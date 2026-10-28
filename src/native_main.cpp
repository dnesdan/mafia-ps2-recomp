#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
// Darwin declares mkdtemp in unistd.h's extension namespace. Select that
// namespace before any transitive system header establishes feature visibility.
#define _DARWIN_C_SOURCE 1
#endif

#include "mafia/verified_elf.hpp"
#include "mafia/guest_executor.hpp"
#include "mafia/gs_capture.hpp"
#include "mafia/controller_input.hpp"
#include "mafia_build_profile.hpp"
#include "ps2_runtime.h"
#include "ps2_host_backend.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_controller.h"

#include <algorithm>
#include <array>
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
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Options {
    fs::path elf, iso, data_root, save_root, report, capture_frame;
    std::uint64_t timeout_ms = 10000;
    std::vector<mafia::PadEvent> pad_events;
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
            else if (arg == "--capture-frame") options.capture_frame = value;
            else if (arg == "--pad-event") {
                if (options.pad_events.size() >= 256)
                    throw std::runtime_error("At most 256 --pad-event pulses are supported per run");
                options.pad_events.push_back(mafia::parse_pad_event(value));
            }
            else if (arg == "--timeout-ms") {
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.timeout_ms);
                if (error != std::errc{} || end != value.data() + value.size() || options.timeout_ms > 86400000)
                    throw std::runtime_error("--timeout-ms must be 0..86400000 (0 disables the deadline)");
            } else throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    if (options.help) return options;
    if (!options.pad_events.empty() && (!options.headless || options.validate_only))
        throw std::runtime_error("--pad-event requires --headless guest execution");
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
    if (!options.capture_frame.empty()) {
        options.capture_frame = fs::absolute(options.capture_frame);
        if (options.validate_only)
            throw std::runtime_error("--capture-frame requires guest execution; remove --validate-only");
        if (options.capture_frame.extension() != ".ppm")
            throw std::runtime_error("--capture-frame requires a .ppm filename (unscaled GS RGB pixels)");
        if (fs::exists(options.capture_frame) || fs::is_symlink(options.capture_frame))
            throw std::runtime_error("Capture output already exists; choose a fresh .ppm filename");
        if (!options.report.empty() &&
            fs::weakly_canonical(options.capture_frame) == fs::weakly_canonical(options.report))
            throw std::runtime_error("Capture and report must use different output paths");
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
    std::optional<mafia::FunctionSymbol> symbol, branch_symbol;
    std::string symbol_error;
    std::uint64_t presentations = 0, dma = 0, gif = 0, gs = 0, vif = 0;
    bool ee_metrics_available = false, io_metrics_available = false;
    EeKernelSnapshot ee;
    std::string ee_final_context = "null", ee_context_error;
    std::vector<std::pair<std::string, std::uint32_t>> io_registers;
    std::string ee_error, io_error;
    bool iop_metrics_available = false;
    ps2x::iop::DebugSnapshot iop;
    std::string iop_error;
    std::string latest_sce_open_request = "null", sce_open_request_error;
    std::string frontend_snapshot = "null", frontend_snapshot_error;
    std::string capture_status = "not-executed", capture_error, capture_sha256;
    std::uint64_t controller_samples = 0;
    mafia::GsFrameCapture capture;
};

const char *thread_status(EeThreadStatus status) {
    switch (status) {
    case EeThreadStatus::Running: return "running";
    case EeThreadStatus::Ready: return "ready";
    case EeThreadStatus::Waiting: return "waiting";
    case EeThreadStatus::WaitingSuspended: return "waiting-suspended";
    case EeThreadStatus::Suspended: return "suspended";
    case EeThreadStatus::Dormant: return "dormant";
    }
    return "unknown";
}

const char *wait_reason(EeWaitReason reason) {
    switch (reason) {
    case EeWaitReason::None: return "none";
    case EeWaitReason::Sleep: return "sleep";
    case EeWaitReason::Semaphore: return "semaphore";
    case EeWaitReason::EventFlag: return "event-flag";
    case EeWaitReason::VSync: return "vsync";
    case EeWaitReason::External: return "external";
    case EeWaitReason::Mpeg: return "mpeg";
    }
    return "unknown";
}

std::string ee_json(const Report &report) {
    if (!report.ee_metrics_available) return "null";
    std::string threads = "[", semaphores = "[";
    for (const auto &thread : report.ee.threads) {
        if (threads.size() != 1) threads += ", ";
        threads += "{\"id\": " + std::to_string(thread.id) +
            ", \"pc\": " + std::to_string(thread.pc) +
            ", \"ra\": " + std::to_string(thread.ra) +
            ", \"entry\": " + std::to_string(thread.entry) +
            ", \"gp\": " + std::to_string(thread.contextGp) +
            ", \"priority\": " + std::to_string(thread.currentPriority) +
            ", \"status\": " + mafia::json_string(thread_status(thread.status)) +
            ", \"wait_reason\": " + mafia::json_string(wait_reason(thread.waitReason)) +
            ", \"wait_id\": " + std::to_string(thread.waitId) +
            ", \"suspend_count\": " + std::to_string(thread.suspendCount) +
            ", \"invocation_depth\": " + std::to_string(thread.invocationDepth) + "}";
    }
    for (const auto &semaphore : report.ee.semaphores) {
        if (semaphores.size() != 1) semaphores += ", ";
        semaphores += "{\"id\": " + std::to_string(semaphore.id) +
            ", \"count\": " + std::to_string(semaphore.count) +
            ", \"max_count\": " + std::to_string(semaphore.maxCount) +
            ", \"waiters\": " + std::to_string(semaphore.waiters) + "}";
    }
    return "{\n    \"sequence\": " + std::to_string(report.ee.sequence) +
        ",\n    \"cycles\": " + std::to_string(report.ee.eeCycle) +
        ",\n    \"next_event_cycle\": " + std::to_string(report.ee.nextEventCycle) +
        ",\n    \"running_thread_id\": " + std::to_string(report.ee.runningThreadId) +
        ",\n    \"threads\": " + threads + "],\n    \"semaphores\": " + semaphores + "]\n  }";
}

void emit_report(const Options &options, const Report &report) {
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    std::string pad_events = "[";
    for (const auto &event : options.pad_events) {
        if (pad_events.size() != 1) pad_events += ", ";
        pad_events += "{\"at_ms\": " + std::to_string(event.at_ms) +
            ", \"duration_ms\": " + std::to_string(event.duration_ms) +
            ", \"button_mask\": " + std::to_string(event.button) + "}";
    }
    pad_events += "]";
    std::string io_json = "{";
    for (const auto &[name, value] : report.io_registers) {
        if (io_json.size() != 1) io_json += ", ";
        io_json += mafia::json_string(name) + ": " + std::to_string(value);
    }
    io_json += "}";
    std::string iop_diagnostics = "[";
    for (const auto &diagnostic : report.iop.diagnostics) {
        if (iop_diagnostics.size() != 1) iop_diagnostics += ", ";
        iop_diagnostics += mafia::json_string(diagnostic);
    }
    iop_diagnostics += "]";
    std::string iop_services = "[";
    for (const auto &service : report.iop.services) {
        if (iop_services.size() != 1) iop_services += ", ";
        std::string sids = "[", metrics = "{";
        for (const auto sid : service.sids) {
            if (sids.size() != 1) sids += ", ";
            sids += std::to_string(sid);
        }
        for (const auto &metric : service.metrics) {
            if (metrics.size() != 1) metrics += ", ";
            metrics += mafia::json_string(metric.name) + ": " + std::to_string(metric.value);
        }
        iop_services += "{\"name\": " + mafia::json_string(service.name) +
            ", \"active\": " + boolean(service.active) + ", \"sids\": " + sids +
            "], \"metrics\": " + metrics + "}}";
    }
    iop_services += "]";
    const std::string iop_json = !report.iop_metrics_available ? "null" :
        "{\n    \"cycles\": " + std::to_string(report.iop.emulatorCycles) +
        ",\n    \"instructions\": " + std::to_string(report.iop.emulatorInstructions) +
        ",\n    \"loaded_guest_modules\": " + std::to_string(report.iop.emulatorLoadedModules) +
        ",\n    \"guest_threads\": " + std::to_string(report.iop.emulatorThreads) +
        ",\n    \"guest_rpc_servers\": " + std::to_string(report.iop.emulatorRpcServers) +
        ",\n    \"services\": " + iop_services +
        ",\n    \"diagnostics\": " + iop_diagnostics + "\n  }";
    const auto &frame = report.capture;
    const std::string capture_json = options.capture_frame.empty() ? "null" :
        "{\n    \"status\": " + mafia::json_string(report.capture_status) +
        ",\n    \"path\": " + mafia::json_string(options.capture_frame.string()) +
        ",\n    \"format\": \"ppm\",\n    \"source\": \"gs-presentation\"" +
        ",\n    \"error\": " + mafia::json_string(report.capture_error) +
        ",\n    \"sha256\": " + mafia::json_string(report.capture_sha256) +
        ",\n    \"width\": " + std::to_string(frame.width) +
        ",\n    \"height\": " + std::to_string(frame.height) +
        ",\n    \"vsync_tick\": " + std::to_string(frame.vsync_tick) +
        ",\n    \"display_fbp\": " + std::to_string(frame.display_fbp) +
        ",\n    \"source_fbp\": " + std::to_string(frame.source_fbp) +
        ",\n    \"used_preferred_source\": " + boolean(frame.used_preferred) +
        ",\n    \"nonblack_pixels\": " + std::to_string(frame.encoded.nonblack_pixels) + "\n  }";
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
        ",\n  \"symbol\": " + (report.symbol ? mafia::json_string(report.symbol->name) : "null") +
        ",\n  \"symbol_address\": " + (report.symbol ? std::to_string(report.symbol->address) : "null") +
        ",\n  \"symbol_offset\": " + (report.symbol ? std::to_string(report.symbol->offset) : "null") +
        ",\n  \"branch_symbol\": " + (report.branch_symbol ? mafia::json_string(report.branch_symbol->name) : "null") +
        ",\n  \"branch_symbol_address\": " + (report.branch_symbol ? std::to_string(report.branch_symbol->address) : "null") +
        ",\n  \"branch_symbol_offset\": " + (report.branch_symbol ? std::to_string(report.branch_symbol->offset) : "null") +
        ",\n  \"symbol_source\": " + (report.symbol || report.branch_symbol ? "\"verified-elf\"" : "null") +
        ",\n  \"symbol_lookup_error\": " + mafia::json_string(report.symbol_error) +
        ",\n  \"gameplay_verified\": false,\n  \"game_frame_verified\": false" +
        ",\n  \"ffmpeg_enabled\": " + boolean(mafia::build::ffmpeg_enabled) +
        ",\n  \"presentations\": " + std::to_string(report.presentations) +
        ",\n  \"controller_input\": {\"route\": \"iop-sio2\", \"port\": 0, \"source\": " +
            mafia::json_string(options.headless ? "headless-timeline" : "window-backend") +
            ", \"host_samples\": " + std::to_string(report.controller_samples) +
            ", \"time_base\": \"host-elapsed-ms\", \"events\": " + pad_events + "}" +
        ",\n  \"dma_starts\": " + std::to_string(report.dma) +
        ",\n  \"gif_copies\": " + std::to_string(report.gif) +
        ",\n  \"gs_writes\": " + std::to_string(report.gs) +
        ",\n  \"vif_writes\": " + std::to_string(report.vif) +
        ",\n  \"ee\": " + ee_json(report) +
        ",\n  \"ee_snapshot_error\": " + mafia::json_string(report.ee_error) +
        ",\n  \"ee_final_context\": " + report.ee_final_context +
        ",\n  \"ee_context_snapshot_error\": " + mafia::json_string(report.ee_context_error) +
        ",\n  \"io_registers\": " + (report.io_metrics_available ? io_json : "null") +
        ",\n  \"io_snapshot_error\": " + mafia::json_string(report.io_error) +
        ",\n  \"iop\": " + iop_json +
        ",\n  \"iop_snapshot_error\": " + mafia::json_string(report.iop_error) +
        ",\n  \"latest_sce_open_request\": " + report.latest_sce_open_request +
        ",\n  \"sce_open_request_snapshot_error\": " + mafia::json_string(report.sce_open_request_error) +
        ",\n  \"frontend_snapshot\": " + report.frontend_snapshot +
        ",\n  \"frontend_snapshot_error\": " + mafia::json_string(report.frontend_snapshot_error) +
        ",\n  \"frame_capture\": " + capture_json + "\n}\n";
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
        if (!runtime.gs().copyLatchedHostPresentationFrame(pixels_, width, height)) {
            // A guest GS reset can disable its display while entering a movie.
            // Clear the host image instead of showing a stale menu indefinitely.
            if (texture_.id) UnloadTexture(texture_);
            texture_ = {};
            have_frame_ = false;
            return false;
        }
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

void collect_symbols(const mafia::VerifiedElf &image, Report &report) {
    try {
        report.symbol = mafia::resolve_function_symbol(image, report.last_pc);
        report.branch_symbol = mafia::resolve_function_symbol(image, report.last_branch_pc);
    } catch (const std::exception &error) {
        report.symbol.reset();
        report.branch_symbol.reset();
        report.symbol_error = error.what();
    }
}

void collect_sce_open_request(PS2Runtime &runtime, Report &report) {
    report.latest_sce_open_request = "null";
    report.sce_open_request_error.clear();
    // This address and layout come from sceOpen in this exact verified ELF.
    // The shared EE request is useful evidence, but is not the IOP's current
    // argument registers and does not establish which request it is executing.
    if (!report.elf_verified || !report.entered ||
        std::string_view(mafia::build::elf_sha256) !=
            "adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f" ||
        report.error.find("missing import ioman:4") == std::string::npos)
        return;
    try {
        constexpr std::uint32_t address = 0x00538100u, size = 0x418u;
        static_assert(address <= PS2_RAM_SIZE && size <= PS2_RAM_SIZE - address);
        const auto *ram = runtime.memory().getRDRAM();
        if (!ram) throw std::runtime_error("EE RAM is unavailable for the latest sceOpen request");
        const auto *request = ram + address;
        const auto word = [&](std::uint32_t offset) {
            return std::uint32_t{request[offset]} | (std::uint32_t{request[offset + 1]} << 8u) |
                (std::uint32_t{request[offset + 2]} << 16u) | (std::uint32_t{request[offset + 3]} << 24u);
        };
        constexpr char hex[] = "0123456789abcdef";
        std::string path, path_hex;
        bool ascii = true, terminated = false;
        for (std::uint32_t i = 0; i < 1024u; ++i) {
            const auto byte = request[0x14u + i];
            if (!byte) { terminated = true; break; }
            ascii = ascii && byte < 0x80u;
            path += static_cast<char>(byte);
            path_hex += hex[byte >> 4u];
            path_hex += hex[byte & 15u];
        }
        report.latest_sce_open_request = "{\"source\": \"verified-elf-shared-ee-request-buffer\""
            ", \"address\": " + std::to_string(address) +
            ", \"size\": " + std::to_string(size) +
            ", \"semaphore_id\": " + std::to_string(word(0)) +
            ", \"result_address\": " + std::to_string(word(4)) +
            ", \"result_length\": " + std::to_string(word(8)) +
            ", \"flags\": " + std::to_string(word(0xcu)) +
            ", \"mode\": " + std::to_string(word(0x10u)) +
            ", \"path\": " + (ascii ? mafia::json_string(path) : "null") +
            ", \"path_bytes_hex\": " + mafia::json_string(path_hex) +
            ", \"path_terminated\": " + (terminated ? "true" : "false") +
            ", \"iob_slot\": " + std::to_string(word(0x414u)) + "}";
    } catch (const std::exception &error) {
        report.sce_open_request_error = error.what();
    }
}

void collect_ee_context(PS2Runtime &runtime, const R5900Context &context, Report &report) {
    report.ee_final_context = "null";
    report.ee_context_error.clear();
    try {
        constexpr const char *names[] = {
            "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
            "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
            "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
        };
        std::string registers = "{";
        for (std::uint32_t i = 0; i < 32u; ++i) {
            if (i) registers += ", ";
            registers += mafia::json_string(names[i]) + ": " +
                std::to_string(static_cast<std::uint32_t>(_mm_cvtsi128_si32(context.r[i])));
        }
        registers += "}";
        std::string stack = "null", stack_status = "guest-not-entered";
        if (report.elf_loaded && report.entered) {
            // Snapshot bytes only; do not dereference arbitrary guest pointers
            // through MMIO or interpret stack words as verified return addresses.
            const auto sp = static_cast<std::uint32_t>(_mm_cvtsi128_si32(context.r[29]));
            const auto segment = sp & 0xe0000000u, physical = sp & 0x1fffffffu;
            const auto *ram = runtime.memory().getRDRAM();
            stack_status = "outside-physical-ram";
            if (ram && sp != 0u && physical < PS2_RAM_SIZE &&
                (segment == 0u || segment == 0x80000000u || segment == 0xa0000000u)) {
                constexpr char hex[] = "0123456789abcdef";
                const auto size = std::min(1024u, PS2_RAM_SIZE - physical);
                std::string bytes;
                bytes.reserve(size * 2u);
                for (std::uint32_t i = 0; i < size; ++i) {
                    const auto byte = ram[physical + i];
                    bytes += hex[byte >> 4u];
                    bytes += hex[byte & 15u];
                }
                stack = "{\"guest_address\": " + std::to_string(sp) +
                    ", \"physical_address\": " + std::to_string(physical) +
                    ", \"size\": " + std::to_string(size) +
                    ", \"bytes_hex\": " + mafia::json_string(bytes) + "}";
                stack_status = "captured";
            }
        }
        report.ee_final_context = "{\"gpr_low32\": " + registers +
            ", \"stack_status\": " + mafia::json_string(stack_status) +
            ", \"stack\": " + stack + "}";
    } catch (const std::exception &error) {
        report.ee_context_error = error.what();
    }
}

void collect_frontend_snapshot(PS2Runtime &runtime, Report &report) {
    report.frontend_snapshot = "null";
    report.frontend_snapshot_error.clear();
    if (!report.elf_verified || !report.elf_loaded || !report.entered ||
        std::string_view(mafia::build::elf_sha256) !=
            "adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f")
        return;
    try {
        // Exact-ELF globals and class offsets verified against the original
        // instructions. Read only after join; never follow pointers into MMIO.
        const auto *ram = runtime.memory().getRDRAM();
        if (!ram) throw std::runtime_error("EE RAM is unavailable for the frontend snapshot");
        const auto span = [](std::uint32_t address, std::uint32_t size) {
            const auto segment = address & 0xe0000000u;
            const auto physical = address & 0x1fffffffu;
            return (segment == 0u || segment == 0x80000000u || segment == 0xa0000000u) &&
                (address & 3u) == 0u && physical < PS2_RAM_SIZE && size <= PS2_RAM_SIZE - physical;
        };
        const auto word = [&](std::uint32_t address) {
            if (!span(address, 4)) throw std::runtime_error("Frontend word is outside aligned EE RAM");
            const auto *p = ram + (address & 0x1fffffffu);
            return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8u) |
                (std::uint32_t{p[2]} << 16u) | (std::uint32_t{p[3]} << 24u);
        };
        const auto kind = [](std::uint32_t vtable) -> const char * {
            switch (vtable) {
            case 0x004d8ac0u: return "autosave-caution";
            case 0x004d8e80u: return "no-card";
            case 0x004d85c0u: return "press-start";
            case 0x004d94c0u: return "profile-select";
            case 0x004d9470u: return "profile-create";
            case 0x004d89d0u: return "main-menu";
            default: return "unknown";
            }
        };
        const auto active = word(0x004dc898u);
        auto address = active;
        std::array<std::uint32_t, 8> seen{};
        std::size_t depth = 0;
        std::string menus = "[", chain_status = "complete";
        while (address) {
            if (!span(address, 0x30u)) { chain_status = "invalid-pointer"; break; }
            const auto physical = address & 0x1fffffffu;
            if (std::find(seen.begin(), seen.begin() + depth, physical) != seen.begin() + depth) {
                chain_status = "cycle"; break;
            }
            if (depth == seen.size()) { chain_status = "depth-limit"; break; }
            seen[depth++] = physical;
            const auto parent = word(address), focus = word(address + 8u);
            const auto vtable = word(address + 0x28u);
            const auto valid_focus = focus != 0u && span(focus, 4);
            if (menus.size() != 1) menus += ", ";
            menus += "{\"address\": " + std::to_string(address) +
                ", \"vtable\": " + std::to_string(vtable) +
                ", \"kind\": " + mafia::json_string(kind(vtable)) +
                ", \"parent\": " + std::to_string(parent) +
                ", \"focus\": " + std::to_string(focus) +
                ", \"focus_id\": " + (valid_focus ? std::to_string(word(focus)) : "null") +
                ", \"word_at_2c\": " + std::to_string(word(address + 0x2cu)) + "}";
            address = parent;
        }
        constexpr std::uint32_t samples_address = 0x0054d280u, samples_size = 64u;
        static_assert(samples_address < PS2_RAM_SIZE && samples_size <= PS2_RAM_SIZE - samples_address);
        constexpr char hex[] = "0123456789abcdef";
        std::string samples;
        for (std::uint32_t i = 0; i < samples_size; ++i) {
            const auto value = ram[samples_address + i];
            samples += hex[value >> 4u]; samples += hex[value & 15u];
        }
        report.frontend_snapshot = "{\"source\": \"verified-elf-read-only-after-join\""
            ", \"active_menu\": " + std::to_string(active) +
            ", \"menu_result\": " + std::to_string(word(0x004dc89cu)) +
            ", \"last_menu_key\": " + std::to_string(word(0x004dc8a4u) & 0xffu) +
            ", \"game_ms\": " + std::to_string(word(0x004dca44u)) +
            ", \"controller_settling\": " + std::to_string(word(0x004dc920u)) +
            ", \"controller_grace\": " + std::to_string(word(0x004dc558u)) +
            ", \"controller_samples_hex\": " + mafia::json_string(samples) +
            ", \"menu_chain_status\": " + mafia::json_string(chain_status) +
            ", \"menus\": " + menus + "]}";
    } catch (const std::exception &error) {
        report.frontend_snapshot_error = error.what();
    }
}

void collect(PS2Runtime &runtime, Report &report) {
    report.metrics_available = true;
    report.entered = entry_executed.load(std::memory_order_acquire);
    // Called only before execution starts or after the worker has joined.
    // The sampled debug PC can lag; the final guest context is authoritative.
    const auto *context = runtime.eeScheduler().currentContext();
    if (!context) context = &runtime.cpu();
    report.last_pc = context->pc;
    report.last_branch_pc = context->branch_pc;
    collect_ee_context(runtime, *context, report);
    report.dma = runtime.memory().dmaStartCount();
    report.gif = runtime.memory().gifCopyCount();
    report.gs = runtime.memory().gsWriteCount();
    report.vif = runtime.memory().vifWriteCount();
    report.ee_metrics_available = false;
    report.ee_error.clear();
    try {
        report.ee = runtime.eeScheduler().snapshot();
        report.ee_metrics_available = true;
    } catch (const std::exception &error) {
        report.ee_error = error.what();
    }
    report.io_metrics_available = false;
    report.io_error.clear();
    report.io_registers.clear();
    try {
        const auto &memory = runtime.memory();
        // Read raw state only after joining the worker. Some upstream MMIO
        // accessors change DMA state, and VIF's live parser fields can differ
        // from the register cache; preserve both without diagnostic side effects.
        const auto raw = [&](const char *name, std::uint32_t address) {
            const auto found = memory.m_ioRegisters.find(address);
            report.io_registers.emplace_back(name, found == memory.m_ioRegisters.end() ? 0u : found->second);
        };
        report.io_registers.emplace_back("vif1_stat", memory.vif1_regs.stat);
        report.io_registers.emplace_back("vif1_code", memory.vif1_regs.code);
        raw("vif1_cached_stat", 0x10003c00u);
        raw("vif1_cached_code", 0x10003c80u);
        raw("vif1_fbrst", 0x10003c10u);
        raw("intc_stat", 0x1000f000u);
        raw("intc_mask", 0x1000f010u);
        raw("d_stat", 0x1000e010u);
        raw("vif1_dma_chcr", 0x10009000u);
        raw("vif1_dma_qwc", 0x10009020u);
        report.io_metrics_available = true;
    } catch (const std::exception &error) {
        report.io_error = error.what();
    }
    report.iop_metrics_available = false;
    report.iop_error.clear();
    try {
        report.iop = runtime.iopDebugSnapshot();
        report.iop_metrics_available = true;
    } catch (const std::exception &error) {
        // Optional diagnostics must not conceal the original guest failure.
        report.iop_error = error.what();
    }
    collect_sce_open_request(runtime, report);
    collect_frontend_snapshot(runtime, report);
}

void sample_controller(PS2Runtime &runtime, const Options &options,
                       std::uint64_t elapsed_ms, Report &report) {
    ps2x::iop::ControllerState state;
    state.connected = true;
    if (options.headless) {
        state.buttons = mafia::pad_buttons_at(options.pad_events, elapsed_ms);
    } else {
        // Raylib input belongs to the window thread. Only a synchronized device
        // snapshot crosses to the executor; the original PADMAN polls SIO2.
        std::array<std::uint8_t, 32> data{};
        if (!runtime.padBackend().readState(0, 0, data.data(), data.size()))
            throw std::runtime_error("Cannot sample the native controller backend");
        state.buttons = static_cast<std::uint16_t>(data[2] | (std::uint16_t{data[3]} << 8));
        state.rightX = data[4]; state.rightY = data[5];
        state.leftX = data[6]; state.leftY = data[7];
    }
    constexpr std::array<std::uint16_t, 12> pressure_masks{
        0x0020, 0x0080, 0x0010, 0x0040, 0x1000, 0x2000,
        0x4000, 0x8000, 0x0400, 0x0800, 0x0100, 0x0200};
    for (std::size_t i = 0; i < pressure_masks.size(); ++i)
        state.pressures[i] = (state.buttons & pressure_masks[i]) ? 0 : 255;
    ps2x::iop::setControllerState(runtime, 0, state);
    ++report.controller_samples;
}
} // namespace

int main(int argc, char **argv) {
    Options options;
    Report report;
    std::optional<mafia::VerifiedElf> image;
    std::unique_ptr<PS2Runtime> runtime;
    int result = 1;
    try {
        options = parse_options(argc, argv);
        if (options.help) {
            std::cout << "MafiaNative --elf PATH [--data-root PATH] [--iso PATH] [--save-root PATH]\n"
                         "  [--headless] [--validate-only] [--timeout-ms 10000] [--report PATH]\n"
                         "  [--capture-frame NEW-PATH.ppm]\n"
                         "  [--headless --pad-event AT_MS:BUTTON:DURATION_MS ...]\n"
                         "Capture saves the unscaled GS surface after execution stops, including in headless mode.\n"
                         "No image is written if the guest did not configure a display surface.\n"
                         "Pad events sample controller port 1 over SIO2; times are elapsed host milliseconds.\n"
                         "Keyboard: Enter START, X/Space Cross, C Circle, arrows/WASD directions.\n"
                         "F11 toggles fullscreen. A timeout/guest stop is a diagnostic failure, not gameplay success.\n";
            return 0;
        }
        report.stage = "elf-verification";
        image = mafia::read_verified_elf(options.elf, mafia::build::elf_sha256,
                                        mafia::build::entry_point);
        report.elf_verified = true;
        report.last_pc = image->entry;
        if (options.validate_only) {
            report.status = "validated-only";
            collect_symbols(*image, report);
            emit_report(options, report);
            return 0;
        }
        VerifiedCopy copy(*image, options.elf);
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
        if (!runtime->hasFunction(image->entry)) throw std::runtime_error("The generated function table does not contain the original entry point");
        original_entry = runtime->lookupFunction(image->entry);
        if (!runtime->replaceFunction(image->entry, &observe_entry))
            throw std::runtime_error("Cannot install original-entry execution observation");
        report.stage = "guest-execution";
        const auto started = Clock::now();
        sample_controller(*runtime, options, 0, report);
        bool timeout = false, closed = false;
        {
            mafia::GuestExecutor executor(*runtime);
            Presentation presentation;
            std::exception_ptr host_error;
            try {
                while (!executor.done() && !runtime->isStopRequested()) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
                    if (options.timeout_ms && elapsed >= std::chrono::milliseconds(options.timeout_ms)) {
                        timeout = true;
                        runtime->requestStop();
                        break;
                    }
                    sample_controller(*runtime, options, static_cast<std::uint64_t>(elapsed.count()), report);
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
                report.capture_status = "executor-still-running";
                report.capture_error = "Cannot snapshot mutable GS state until the EE executor has joined";
                report.entered = entry_executed.load(std::memory_order_acquire);
                report.last_pc = runtime->m_debugPc.load(std::memory_order_relaxed);
                // Symbols come from immutable verified input, independent of
                // guest memory even when the executor has not stopped.
                collect_symbols(*image, report);
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
    if (image) collect_symbols(*image, report);
    if (!options.capture_frame.empty()) {
        // All ordinary and exceptional paths above join the EE worker before
        // inspecting mutable state. The unresponsive-worker path exits earlier.
        try {
            if (!runtime || !report.elf_loaded || !report.entered) {
                report.capture_status = "not-executed";
                report.capture_error = "Guest entry did not execute; no framebuffer evidence is available";
            } else {
                report.capture = mafia::capture_gs_frame(*runtime);
                if (!report.capture.available) {
                    report.capture_status = "unavailable";
                    report.capture_error = "The GS has no valid presentation surface at capture time";
                } else {
                    const auto digest = mafia::sha256(report.capture.encoded.ppm);
                    mafia::save_frame_exclusive(options.capture_frame, report.capture.encoded.ppm);
                    report.capture_sha256 = digest;
                    report.capture_status = "saved";
                }
            }
        } catch (const std::exception &error) {
            report.capture_status = "failed";
            report.capture_error = error.what();
        }
        if (report.capture_status != "saved") {
            std::cerr << "MafiaNative capture: " << report.capture_error << '\n';
            if (result == 0) {
                result = 1;
                report.status = "capture-unavailable";
                report.error = report.capture_error;
            }
        }
    }
    if (!options.headless && IsWindowReady()) CloseWindow();
    try { emit_report(options, report); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return result;
}
