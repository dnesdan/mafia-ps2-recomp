#include "mafia/gs_capture.hpp"
#include "ps2_runtime.h"

namespace mafia {

GsFrameCapture capture_gs_frame(PS2Runtime &runtime) {
    GsFrameCapture result;
    result.vsync_tick = runtime.memory().gs().vsyncTick.load(std::memory_order_acquire);
    runtime.gs().latchHostPresentationFrame();
    std::vector<std::uint8_t> rgba;
    result.available = runtime.gs().copyLatchedHostPresentationFrame(
        rgba, result.width, result.height, &result.display_fbp, &result.source_fbp,
        &result.used_preferred);
    if (result.available)
        result.encoded = encode_ppm_frame(rgba, result.width, result.height);
    return result;
}

} // namespace mafia
