#pragma once

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

namespace mafia {

// Owns the EE execution thread. All non-atomic guest inspection must happen
// after join(); shutdown remains bounded even while unwinding a host exception.
class GuestExecutor {
public:
    explicit GuestExecutor(PS2Runtime &runtime) : runtime_(runtime), thread_([this] {
        try {
            runtime_.eeScheduler().reset(runtime_.memory().getRDRAM(), runtime_.cpu());
            started_.store(true, std::memory_order_release);
            started_.notify_all();
            runtime_.eeScheduler().run();
        } catch (...) { error_ = std::current_exception(); }
        done_.store(true, std::memory_order_release);
        started_.store(true, std::memory_order_release);
        started_.notify_all();
    }) {
        // reset() clears the scheduler stop flag. Do not let a deadline/close
        // request race ahead of it and disappear.
        started_.wait(false, std::memory_order_acquire);
    }

    ~GuestExecutor() {
        if (!thread_.joinable()) return;
        runtime_.requestStop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (!done()) {
            // Main normally writes a structured stop-timeout report. This is
            // the last-resort unwind path, with only atomic state safe to read.
            std::fprintf(stderr, "MafiaNative: EE shutdown timed out during unwinding; sampled guest_pc=0x%08X\n",
                         static_cast<unsigned>(runtime_.m_debugPc.load(std::memory_order_relaxed)));
            std::fflush(stderr);
            std::_Exit(124);
        }
        thread_.join();
    }

    bool done() const { return done_.load(std::memory_order_acquire); }
    void join() {
        if (thread_.joinable()) thread_.join();
        if (error_) std::rethrow_exception(error_);
    }

    GuestExecutor(const GuestExecutor &) = delete;
    GuestExecutor &operator=(const GuestExecutor &) = delete;

private:
    PS2Runtime &runtime_;
    std::atomic<bool> started_{false};
    std::atomic<bool> done_{false};
    std::exception_ptr error_;
    std::thread thread_;
};
} // namespace mafia
