// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

// The library's kick and wait for idle (0x243EE7, called from the texture upload at 0x243C3F): it kicks the
// pushbuffer through 0x244620 and spins until the NV2A reports the channel idle at miniport + 0x100 bit 24,
// which no host code raises because the drawing is the host's. Nothing is left to wait for, and the value it
// hands back is the completion the caller asked for, zero.
X86_FUNC(sub_00243EE7) {
    uint32_t out = R32(ctx.esp + 4);
    if (out) W32(out, 0);
}
