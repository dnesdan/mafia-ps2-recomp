#include "mafia/verified_elf.hpp"
#include "mafia/guest_executor.hpp"
#include "mafia_build_profile.hpp"
#include "ps2_runtime.h"
#include "ps2_host_backend.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Options {
    fs::path elf, iso, data_root, save_root, report;
    std::uint64_t timeout_ms = 10000;
    bool headless = false, validate_only = false, help = false;
};

Options parse_options(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") options.help = true;
        else if (arg == "--headless") options.headless = true;
        else if (arg == "--validate-only") options.validate_only = true;
        else {
            if (i + 1 == argc) throw std::runtime_error("Missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--elf") options.elf = value;
            else if (arg == "--iso") options.iso = value;
            else if (arg == "--data-root") options.data_root = value;
            else if (arg == "--save-root") options.save_root = value;
            else if (arg == "--report") options.report = value;
            else if (arg == "--timeout-ms") {
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.timeout_ms);
                if (error != std::errc{} || end != value.data() + value.size() || options.timeout_ms > 86400000)
                    throw std::runtime_error("--timeout-ms must be 0..86400000 (0 disables the deadline)");
            } else throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    if (options.help) return options;
    if (options.elf.empty()) throw std::runtime_error("Pass --elf with the original executable matching this build");
    options.elf = fs::absolute(options.elf);
    if (options.data_root.empty()) options.data_root = options.elf.parent_path();
    options.data_root = fs::absolute(options.data_root);
    if (!fs::is_directory(options.data_root)) throw std::runtime_error("Data root is not a directory");
    if (!options.iso.empty()) {
        options.iso = fs::absolute(options.iso);
        if (!fs::is_regular_file(options.iso)) throw std::runtime_error("ISO does not exist");
    }
    if (options.save_root.empty()) {
#if defined(__APPLE__)
        if (const char *home = std::getenv("HOME"))
            options.save_root = fs::path(home) / "Library/Application Support/MafiaNative/memory-cards";
        else throw std::runtime_error("HOME is unavailable; specify --save-root");
#else
        options.save_root = fs::current_path() / ".local/memory-cards";
#endif
    }
    options.save_root = fs::absolute(options.save_root);
    if (!options.report.empty()) {
        options.report = fs::absolute(options.report);
        if (fs::weakly_canonical(options.report) == fs::weakly_canonical(options.elf) ||
            (!options.iso.empty() && fs::weakly_canonical(options.report) == fs::weakly_canonical(options.iso)) ||
            (fs::exists(options.report) && (fs::equivalent(options.report,options.elf) ||
                (!options.iso.empty() && fs::equivalent(options.report,options.iso)))))
            throw std::runtime_error("Report output must not overwrite a game input");
    }
    return options;
}

// Load exactly the bytes that passed validation, preserving the serial basename
// used by the upstream ROM/IOP identity resolver. This copy is private and temporary.
class VerifiedCopy {
public:
    VerifiedCopy(const mafia::VerifiedElf &image, const fs::path &original) {
        std::string pattern = (fs::temp_directory_path() / "mafia-verified-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char *created = ::mkdtemp(buffer.data());
        if (!created) throw std::runtime_error("Cannot create a private verified ELF directory");
        directory_ = created;
        path_ = directory_ / original.filename();
        try {
            std::ofstream file(path_, std::ios::binary);
            file.write(reinterpret_cast<const char *>(image.bytes.data()),
                       static_cast<std::streamsize>(image.bytes.size()));
            file.close();
            if (!file) throw std::runtime_error("Cannot stage the verified ELF bytes");
        } catch (...) {
            std::error_code error;
            fs::remove_all(directory_, error);
            throw;
        }
    }
    ~VerifiedCopy() { std::error_code error; fs::remove_all(directory_, error); }
    const fs::path &path() const { return path_; }
    VerifiedCopy(const VerifiedCopy &) = delete;
    VerifiedCopy &operator=(const VerifiedCopy &) = delete;
private:
    fs::path directory_, path_;
};

std::atomic<bool> entry_executed{false};
PS2Runtime::RecompiledFunction original_entry = nullptr;
void observe_entry(std::uint8_t *ram, R5900Context *context, PS2Runtime *runtime) {
    entry_executed.store(true, std::memory_order_release);
    runtime->replaceFunction(mafia::build::entry_point, original_entry);
    original_entry(ram, context, runtime);
}

struct Report {
    std::string status = "failed", stage = "arguments", error;
    bool elf_verified = false, elf_loaded = false, entered = false, metrics_available = false;
    std::uint32_t last_pc = 0, last_branch_pc = 0;
    std::uint64_t presentations = 0, dma = 0, gif = 0, gs = 0, vif = 0;
};

void emit_report(const Options &options, const Report &report) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    const std::string json = "{\n  \"schema_version\": 1,\n  \"profile_id\": " + mafia::json_string(mafia::build::profile_id) +
        ",\n  \"elf_sha256\": " + mafia::json_string(mafia::build::elf_sha256) +
        ",\n  \"ps2recomp_commit\": " + mafia::json_string(mafia::build::ps2recomp_commit) +
        ",\n  \"status\": " + mafia::json_string(report.status) +
        ",\n  \"stage\": " + mafia::json_string(report.stage) +
        ",\n  \"error\": " + mafia::json_string(report.error) +
        ",\n  \"elf_verified\": " + boolean(report.elf_verified) +
        ",\n  \"elf_loaded\": " + boolean(report.elf_loaded) +
        ",\n  \"entry_executed\": " + boolean(report.entered) +
        ",\n  \"metrics_available\": " + boolean(report.metrics_available) +
        ",\n  \"last_guest_pc\": " + std::to_string(report.last_pc) +
        ",\n  \"last_guest_branch_pc\": " + std::to_string(report.last_branch_pc) +
        ",\n  \"symbol\": null,\n  \"gameplay_verified\": false,\n  \"game_frame_verified\": false" +
        ",\n  \"ffmpeg_enabled\": " + boolean(mafia::build::ffmpeg_enabled) +
        ",\n  \"presentations\": " + std::to_string(report.presentations) +
        ",\n  \"dma_starts\": " + std::to_string(report.dma) +
        ",\n  \"gif_copies\": " + std::to_string(report.gif) +
        ",\n  \"gs_writes\": " + std::to_string(report.gs) +
        ",\n  \"vif_writes\": " + std::to_string(report.vif) + "\n}\n";
    std::cout << json;
    if (!options.report.empty()) {
        fs::create_directories(options.report.parent_path());
        std::string pattern=(options.report.parent_path()/".mafia-report-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());
        name.push_back('\0');
        const char *created=::mkdtemp(name.data());
        if(!created) throw std::runtime_error("Cannot create a private report staging directory");
        const fs::path temporary=created;
        try {
            const auto file_path=temporary/"report.json";
            std::ofstream file(file_path);
            file << json;
            file.close();
            if (!file) throw std::runtime_error("Cannot write execution report");
            // Atomic replacement changes the directory entry, never the bytes
            // behind a newly introduced hardlink or symlink to a game input.
            fs::rename(file_path,options.report);
        } catch(...) {
            std::error_code error;
            fs::remove_all(temporary,error);
            throw;
        }
        std::error_code error;
        fs::remove_all(temporary,error);
    }
}

class Presentation {
public:
    ~Presentation() { if (texture_.id) UnloadTexture(texture_); }
    bool update(PS2Runtime &runtime) {
        // The scheduler's raw currentVSyncTick() accessor is executor-owned.
        // Its GS mirror is atomic and safe for the presentation thread.
        const auto tick=runtime.memory().gs().vsyncTick.load(std::memory_order_acquire);
        if(have_frame_ && tick==last_tick_) return false;
        // The GS owns the conversion of guest VRAM; the host never substitutes
        // an image when the guest has not produced a valid presentation surface.
        runtime.gs().latchHostPresentationFrame();
        std::uint32_t width = 0, height = 0;
        if (!runtime.gs().copyLatchedHostPresentationFrame(pixels_, width, height)) return false;
        if (width == 0 || height == 0 || width > 4096 || height > 4096 ||
            std::uint64_t{width} * height * 4 != pixels_.size())
            throw std::runtime_error("GS presentation surface has invalid dimensions");
        if (texture_.width != static_cast<int>(width) || texture_.height != static_cast<int>(height)) {
            if (texture_.id) UnloadTexture(texture_);
            const Image image{pixels_.data(), static_cast<int>(width), static_cast<int>(height), 1,
                              PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            texture_ = LoadTextureFromImage(image);
            if (!texture_.id) throw std::runtime_error("Cannot upload the GS presentation surface");
        } else UpdateTexture(texture_, pixels_.data());
        last_tick_=tick;
        have_frame_=true;
        return true;
    }
    void draw() const {
        BeginDrawing();
        ClearBackground(BLACK);
        if (texture_.id) {
            const auto width = static_cast<float>(texture_.width);
            const auto height = static_cast<float>(texture_.height);
            const auto screen_width = static_cast<float>(GetScreenWidth());
            const auto screen_height = static_cast<float>(GetScreenHeight());
            // PS2 framebuffer pixels can be non-square (including PAL). Keep
            // the original 4:3 display shape instead of inferring it from VRAM.
            constexpr float display_aspect = 4.0f/3.0f;
            const auto display_height = std::min(screen_height,screen_width/display_aspect);
            const auto display_width = display_height*display_aspect;
            const Rectangle source{0, 0, width, height};
            const Rectangle destination{(screen_width-display_width)/2, (screen_height-display_height)/2,
                                        display_width, display_height};
            DrawTexturePro(texture_, source, destination, Vector2{0,0}, 0, WHITE);
        }
        EndDrawing();
    }
private:
    Texture2D texture_{};
    std::vector<std::uint8_t> pixels_;
    std::uint64_t last_tick_{};
    bool have_frame_{};
};

void collect(PS2Runtime &runtime, Report &report) {
    report.metrics_available = true;
    report.entered = entry_executed.load(std::memory_order_acquire);
    // Called only before execution starts or after the worker has joined.
    // The sampled debug PC can lag; the final guest context is authoritative.
    const auto *context = runtime.eeScheduler().currentContext();
    if (!context) context = &runtime.cpu();
    report.last_pc = context->pc;
    report.last_branch_pc = context->branch_pc;
    report.dma = runtime.memory().dmaStartCount();
    report.gif = runtime.memory().gifCopyCount();
    report.gs = runtime.memory().gsWriteCount();
    report.vif = runtime.memory().vifWriteCount();
}
} // namespace

int main(int argc, char **argv) {
    Options options;
    Report report;
    std::unique_ptr<PS2Runtime> runtime;
    int result = 1;
    try {
        options = parse_options(argc, argv);
        if (options.help) {
            std::cout << "MafiaNative --elf PATH [--data-root PATH] [--iso PATH] [--save-root PATH]\n"
                         "  [--headless] [--validate-only] [--timeout-ms 10000] [--report PATH]\n"
                         "F11 toggles fullscreen. A timeout/guest stop is a diagnostic failure, not gameplay success.\n";
            return 0;
        }
        report.stage = "elf-verification";
        const auto image = mafia::read_verified_elf(options.elf, mafia::build::elf_sha256,
                                                   mafia::build::entry_point);
        report.elf_verified = true;
        report.last_pc = image.entry;
        if (options.validate_only) {
            report.status = "validated-only";
            emit_report(options, report);
            return 0;
        }
        VerifiedCopy copy(image, options.elf);
        report.stage = "runtime-initialization";
        runtime = std::make_unique<PS2Runtime>();
        runtime->setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
        if (options.headless) {
            if (!runtime->memory().initialize() || !runtime->syncCoreSubsystems())
                throw std::runtime_error("Cannot initialize EE memory and core subsystems");
        } else {
            if (!runtime->initialize("MafiaNative") || !IsWindowReady())
                throw std::runtime_error("Cannot initialize the native window");
        }
        report.stage = "elf-loading";
        if (!runtime->loadELF(copy.path().string())) throw std::runtime_error("PS2Recomp rejected the verified ELF");
        report.elf_loaded = true;
        PS2Runtime::IoPaths paths;
        paths.elfPath = options.elf;
        paths.elfDirectory = options.elf.parent_path();
        paths.cdRoot = paths.hostRoot = options.data_root;
        paths.mcRoot = options.save_root;
        paths.cdImage = options.iso;
        fs::create_directories(paths.mcRoot);
        PS2Runtime::setIoPaths(paths);
        runtime->initializeEeKernelState(runtime->memory().getRDRAM());
        runtime->cpu().r[4] = _mm_setzero_si128();
        runtime->cpu().r[5] = _mm_setzero_si128();
        runtime->cpu().r[29] = _mm_set_epi64x(0, static_cast<std::int64_t>(PS2_RAM_SIZE - 0x10u));
        if (!runtime->hasFunction(image.entry)) throw std::runtime_error("The generated function table does not contain the original entry point");
        original_entry = runtime->lookupFunction(image.entry);
        if (!runtime->replaceFunction(image.entry, &observe_entry))
            throw std::runtime_error("Cannot install original-entry execution observation");
        report.stage = "guest-execution";
        const auto started = Clock::now();
        bool timeout = false, closed = false;
        {
            mafia::GuestExecutor executor(*runtime);
            Presentation presentation;
            std::exception_ptr host_error;
            try {
                while (!executor.done() && !runtime->isStopRequested()) {
                    if (options.timeout_ms && Clock::now() - started >= std::chrono::milliseconds(options.timeout_ms)) {
                        timeout = true;
                        runtime->requestStop();
                        break;
                    }
                    if (options.headless) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    } else {
                        if (WindowShouldClose()) { closed = true; runtime->requestStop(); break; }
                        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
                        if (presentation.update(*runtime)) ++report.presentations;
                        presentation.draw();
                    }
                }
            } catch (...) {
                host_error = std::current_exception();
                runtime->requestStop();
            }
            // Generated code normally notices a stop at its next scheduler
            // checkpoint. Bound shutdown too: a broken generated loop must not
            // turn a diagnostic timeout into an indefinitely hung process.
            const auto stop_deadline = Clock::now() + std::chrono::seconds(2);
            while (!executor.done() && Clock::now() < stop_deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!executor.done()) {
                report.status = "stop-timeout";
                report.error = "EE executor did not stop within two seconds of a stop request";
                report.entered = entry_executed.load(std::memory_order_acquire);
                report.last_pc = runtime->m_debugPc.load(std::memory_order_relaxed);
                // Do not inspect mutable guest memory or contexts while the
                // executor is still running. Its last sampled PC is all we have.
                try { emit_report(options, report); }
                catch (const std::exception &error) { std::cerr << "Cannot persist stop report: " << error.what() << '\n'; }
                std::cout.flush();
                std::cerr.flush();
                std::_Exit(124);
            }
            try { executor.join(); }
            catch (...) { if (!host_error) host_error = std::current_exception(); }
            if (host_error) std::rethrow_exception(host_error);
        }
        collect(*runtime, report);
        if (timeout) {
            report.status = "timeout";
            report.error = "The diagnostic execution deadline expired";
            result = 124;
        } else if (closed) {
            report.status = "user-closed";
            result = 0;
        } else if (runtime->isStopRequested()) {
            report.status = runtime->hasFunction(report.last_pc) ? "guest-stopped" : "missing-dispatch";
            report.error = "Guest execution requested a stop; inspect the preceding subsystem diagnostics";
        } else if (!report.entered) {
            throw std::runtime_error("Scheduler returned without invoking the original entry point");
        } else {
            report.status = "guest-returned";
            result = 0;
        }
    } catch (const std::exception &error) {
        report.status = "failed";
        report.error = error.what();
        if (runtime) collect(*runtime, report);
        std::cerr << "MafiaNative: " << report.error << " (stage=" << report.stage
                  << ", guest_pc=0x" << std::hex << report.last_pc
                  << ", last_branch_pc=0x" << report.last_branch_pc << std::dec << ")\n";
    }
    if (!options.headless && IsWindowReady()) CloseWindow();
    try { emit_report(options, report); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return result;
}
