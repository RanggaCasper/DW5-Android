#pragma once
#include <atomic>
#include <cstdint>

// The renderer's own settings. The title has no options screen here, so these are the
// defaults a fresh install would use; they are read every frame by d3d/vulkan_linux.cpp.
namespace game {

struct Options {
    bool bloom = true;        // the glow added over bright parts of the frame
    bool fog = true;
    uint32_t msaa = 0;        // samples a pixel of the scene, 0 off
    bool smaa = false;        // over the finished frame
    bool hdr_output = false;  // HDR10 on a display that offers it
    bool fps_counter = true;     // the frame rate in the top left corner (Android)
    bool frame_skip = true;      // frames left undrawn while the drawing is behind, the game at its speed (Android)
    uint32_t aspect = 0;         // the frame's shape on the screen: 0 the TV's, 1 16:9, 2 the whole screen stretched,
                                 // 3 the whole screen covered, what overflows cut (Android)
    uint32_t controls_opacity = 100;  // the on-screen controls' opacity, 20 to 100% (Android)
    uint32_t controls_scale = 100;    // their size, 50 to 150% (Android); the layout editor sets where they sit
    uint32_t controls_style = 1;      // their art: 0 the pad's letters, 1 what each button does in the game (Android)
};

inline Options& Opt() {
    static Options options;
    return options;
}

inline std::atomic<bool> g_hdr_display{false};  // the window's display has HDR on, from the renderer

}  // namespace game
