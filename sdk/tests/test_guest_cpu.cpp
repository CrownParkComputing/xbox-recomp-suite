#include "check.h"
#include "kernel/guest_cpu.h"

#include <atomic>
#include <future>
#include <thread>
#include <vector>

TEST("kernel: guest CPU excludes other guest threads") {
    kernel::GuestCpu cpu;
    cpu.Enable();
    std::atomic<unsigned> inside{0}, overlaps{0};
    unsigned updates = 0;
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] {
        for (unsigned n = 0; n < 1000; ++n) {
            cpu.Enter();
            if (inside.fetch_add(1)) ++overlaps;
            auto before = updates;
            std::this_thread::yield();
            updates = before + 1;
            --inside;
            cpu.Leave();
        }
    });
    for (auto& worker : workers) worker.join();
    CHECK_EQ(overlaps.load(), 0u);
    CHECK_EQ(updates, 4000u);
}

TEST("kernel: guest CPU yields for waits and nested callbacks") {
    kernel::GuestCpu cpu;
    CHECK(!cpu.Enter());  // other titles retain concurrent execution
    cpu.Enable();
    CHECK(cpu.Enter());
    CHECK(!cpu.Enter());  // a callback already on the CPU shares ownership
    std::promise<void> started, callback;
    auto attempt = started.get_future();
    auto completed = callback.get_future();
    std::thread worker([&] {
        started.set_value();
        cpu.Enter();
        callback.set_value();
        cpu.Leave();
    });
    attempt.wait();
    CHECK(completed.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    CHECK(cpu.Leave());  // kernel wait gives the CPU to the callback
    completed.wait();
    CHECK(cpu.Enter());  // guest continuation reacquires it
    CHECK(!cpu.Enter());
    CHECK(cpu.Leave());
    CHECK(!cpu.Leave());  // a host-only nested call cannot release another CPU
    worker.join();
}
