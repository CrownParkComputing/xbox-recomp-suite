#include "kernel/kernel.h"
#include <cstdlib>

// XDK OutputDebugStringA, verified at 0x9B7B1 (stdcall, ret 4 at
// 0x9B7E0). The console sends text through debugger interrupts 0x2D/3;
// on the host retain that diagnostic in the title log instead.
X86_FUNC(sub_0009B7B1) {
    const kernel::Args args{ctx};
    LOGI("OutRun2 debug: %s", kernel::ReadCString(args[0]).c_str());
    kernel::Return(ctx, 0, 1);
}

X86_EXTERN_FUNC(__imp__sub_0005D840);
X86_FUNC(sub_0005D840) {
    static const bool trace = std::getenv("OUTRUN2_TRACE_STARTUP") != nullptr;
    static unsigned frames = 0;
    if (trace && (frames++ % 60) == 0) {
        LOGI("OutRun2 startup: mode=%u pending=%u request=%u sub=%u select=%u vblank=%u board=%u ready=%u inputs=%08X",
             kernel::R32(0x432154), kernel::R32(0x432158), kernel::R32(0x432144),
             kernel::R32(0x432160), kernel::R32(0x4321D8), kernel::R32(0x422A08), kernel::R32(0x421D74),
             kernel::R32(0x536120), kernel::R32(0x421D88));
        const auto job = kernel::R32(0x4FE2B8);
        LOGI("OutRun2 loader: state=%u job=%08X fields=%08X/%08X/%08X/%08X queue=%08X",
             kernel::R32(0x4FE2C0), job, job ? kernel::R32(job) : 0,
             job ? kernel::R32(job + 4) : 0, job ? kernel::R32(job + 8) : 0,
             job ? kernel::R32(job + 12) : 0, kernel::R32(0x3B70C8));
        // 0x5CA3C copies the first three analog channels; 0x5C900
        // normalizes them using the game's original calibration data.
        LOGI("OutRun2 driving: raw=%u/%u/%u normalized=%d/%u/%u",
             kernel::R32(0x421BB8), kernel::R32(0x421BC8), kernel::R32(0x421BD8),
             int32_t(kernel::R32(0x421BBC)), kernel::R32(0x421BCC), kernel::R32(0x421BDC));
    }
    __imp__sub_0005D840(ctx, base);
}
