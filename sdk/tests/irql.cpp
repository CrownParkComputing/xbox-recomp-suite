#include "check.h"
#include "kernel/irql.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

TEST("kernel: IRQL transitions preserve nested dispatch exclusion") {
    kernel::IrqlGate gate;
    uint8_t level = 0;
    gate.Set(level, 1);
    CHECK_EQ(level, 1);
    gate.Set(level, 2);
    CHECK_EQ(level, 2);
    std::promise<void> attempting, entered;
    auto attempted = attempting.get_future();
    auto acquired = entered.get_future();
    std::thread other([&] {
        uint8_t other_level = 0;
        attempting.set_value();
        gate.Set(other_level, 2);
        entered.set_value();
        gate.Set(other_level, 0);
    });
    attempted.wait();
    CHECK(acquired.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    gate.Set(level, 3);
    gate.Set(level, 2);
    CHECK(acquired.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    gate.Set(level, 1);
    other.join();
    CHECK(acquired.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    CHECK_EQ(level, 1);
    gate.Set(level, 0);
    CHECK_EQ(level, 0);
}

TEST("kernel: host threads cannot mutate a dispatch queue concurrently") {
    kernel::IrqlGate gate;
    std::atomic<unsigned> inside{0}, overlaps{0}, updates{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i) threads.emplace_back([&] {
        uint8_t level = 0;
        for (unsigned n = 0; n < 2000; ++n) {
            gate.Set(level, 2);
            if (inside.fetch_add(1) != 0) ++overlaps;
            const auto before = updates.load();
            gate.Set(level, 3);
            std::this_thread::yield();
            gate.Set(level, 2);
            updates = before + 1;
            --inside;
            gate.Set(level, 0);
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK_EQ(overlaps.load(), 0u);
    CHECK_EQ(updates.load(), 8000u);
    CHECK_EQ(inside.load(), 0u);
}
