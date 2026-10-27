#pragma once

#include "mafia/frame_capture.hpp"

class PS2Runtime;

namespace mafia {

struct GsFrameCapture {
    bool available = false;
    std::uint32_t width{}, height{}, display_fbp{}, source_fbp{};
    std::uint64_t vsync_tick{};
    bool used_preferred = false;
    EncodedFrame encoded;
};

// Call only with the EE executor stopped and joined. This snapshots the same
// upstream GS presentation path used by the window, without creating a window.
GsFrameCapture capture_gs_frame(PS2Runtime &runtime);

} // namespace mafia
