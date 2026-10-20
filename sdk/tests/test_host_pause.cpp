#include "check.h"
#include "core/host_pause.h"
#include "kernel/kernel.h"
#include <future>
#include <thread>

TEST("host panel: early R3 waits for the first renderable frame") {
    host::PauseState state;
    state.Menu(true);
    CHECK(!state.Paused());
    state.FrameReady();
    CHECK(state.Paused());
    state.Menu(false);
    CHECK(!state.Paused());
    state.Menu(true);
    CHECK(state.Paused());
    state.Menu(false);
    CHECK(!state.Paused());
}

TEST("host panel: game clock freezes and resumes without menu time") {
    using namespace std::chrono;
    host::PauseState state;
    const auto before = state.Now();
    state.Set(true);
    const auto frozen = state.Now();
    std::this_thread::sleep_for(milliseconds(30));
    CHECK(state.Now() == frozen);
    state.Set(true);  // repeated open must not reset the start of the pause
    CHECK(state.Now() == frozen);
    state.Set(false);
    const auto resumed = state.Now();
    CHECK(resumed >= frozen);
    CHECK(resumed - frozen < milliseconds(20));
    CHECK(state.Excluded() >= milliseconds(30));
    CHECK(before <= resumed);
}

TEST("host panel: guest workers park, resume and survive repeated cycles") {
    using namespace std::chrono;
    host::PauseState state;
    for (int cycle = 0; cycle < 4; ++cycle) {
        state.Set(true);
        std::promise<void> entered, finished;
        auto started = entered.get_future(), done = finished.get_future();
        std::thread worker([&] { entered.set_value(); state.Wait(); finished.set_value(); });
        started.wait();
        CHECK(done.wait_for(milliseconds(15)) == std::future_status::timeout);
        state.Set(false);
        CHECK(done.wait_for(seconds(2)) == std::future_status::ready);
        worker.join();
    }
}

TEST("host panel: IRQL owner finishes before parking at a guest checkpoint") {
    using namespace std::chrono;
    // Only this small KPCR buffer is accessed by HostPausePoint.
    uint8_t kpcr[128]{};
    auto* saved = kernel::g_base;
    kernel::g_base = kpcr;
    X86Context ctx{};
    ctx.fs_base = 16;
    kpcr[16 + 0x24] = 2;
    host::Pause().Set(true);
    kernel::HostPausePoint(ctx);  // cannot block while the dispatch gate is owned
    kpcr[16 + 0x24] = 0;
    auto done = std::async(std::launch::async, [&] { kernel::HostPausePoint(ctx); });
    CHECK(done.wait_for(milliseconds(20)) == std::future_status::timeout);
    host::Pause().Set(false);
    CHECK(done.wait_for(seconds(2)) == std::future_status::ready);
    done.get();
    kernel::g_base = saved;
}
