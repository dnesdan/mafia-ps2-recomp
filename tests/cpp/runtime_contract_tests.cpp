#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "mafia/guest_executor.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <chrono>
#include <string_view>

// Handwritten test addresses/functions. These are not linked into MafiaNative.
extern const std::uint32_t g_ps2RecompiledFunctionTableBase = 0x00100000u;
extern const std::uint32_t g_ps2RecompiledFunctionTableEnd = 0x00100008u;
extern const std::uint32_t g_ps2RecompiledFunctionTableSlotCount = 2u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[2]{};

namespace {
std::atomic<bool> waiting_entered{false};
bool ignore_stop = false;
void synthetic_entry(std::uint8_t *ram, R5900Context *ctx, PS2Runtime *runtime) {
    runtime->Store32(ram,ctx,0x00101000u,0xdecafbad);
    ctx->pc=0x00100004u;
}
void waiting_entry(std::uint8_t *, R5900Context *, PS2Runtime *runtime) {
    waiting_entered.store(true,std::memory_order_release);
    while(ignore_stop || !runtime->isStopRequested())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
}

int main(int argc, char **argv) {
    try {
        PS2Runtime runtime;
        runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
        if(!runtime.memory().initialize() || !runtime.syncCoreSubsystems())
            throw std::runtime_error("Core subsystem initialization failed");
        runtime.Store32(runtime.memory().getRDRAM(),&runtime.cpu(),0x00100000u,0x12345678u);
        if(runtime.Load32(runtime.memory().getRDRAM(),&runtime.cpu(),0x80100000u)!=0x12345678u)
            throw std::runtime_error("EE cached memory alias mismatch");
        runtime.Store32(runtime.memory().getRDRAM(),&runtime.cpu(),0x70000000u,0xa5a5a5a5u);
        if(runtime.Load32(runtime.memory().getRDRAM(),&runtime.cpu(),0x70000000u)!=0xa5a5a5a5u)
            throw std::runtime_error("Scratchpad round trip failed");
        g_ps2RecompiledFunctionTable[0]=synthetic_entry;
        runtime.cpu().pc=0x00100000u;
        std::exception_ptr error;
        std::thread guest([&] {
            try {
                runtime.eeScheduler().reset(runtime.memory().getRDRAM(),runtime.cpu());
                runtime.eeScheduler().run();
            } catch(...) {error=std::current_exception();}
        });
        guest.join();
        if(error) std::rethrow_exception(error);
        if(runtime.Load32(runtime.memory().getRDRAM(),&runtime.cpu(),0x00101000u)!=0xdecafbadu)
            throw std::runtime_error("Scheduler did not invoke the registered synthetic entry");
        if(!runtime.isStopRequested() || runtime.cpu().pc!=0x00100004u)
            throw std::runtime_error("Missing dispatch did not preserve the failed PC and request stop");
        PS2Runtime unwind_runtime;
        unwind_runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
        if(!unwind_runtime.memory().initialize() || !unwind_runtime.syncCoreSubsystems())
            throw std::runtime_error("Unwind test initialization failed");
        g_ps2RecompiledFunctionTable[0]=waiting_entry;
        unwind_runtime.cpu().pc=0x00100000u;
        ignore_stop=argc==2 && std::string_view(argv[1])=="--unresponsive-unwind";
        bool caught=false;
        try {
            mafia::GuestExecutor executor(unwind_runtime);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while(!waiting_entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            throw std::runtime_error("injected presentation failure");
        } catch(const std::runtime_error &) {caught=true;}
        if(!caught || !waiting_entered.load(std::memory_order_acquire) || !unwind_runtime.isStopRequested())
            throw std::runtime_error("Host exception did not stop and join the EE worker");
        std::cout<<"Runtime contracts passed (memory, scratchpad, scheduler entry, strict dispatch, exception shutdown; no game execution).\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
