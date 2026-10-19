#pragma once
#include <cstdint>
#include <mutex>

namespace kernel {

// Xbox titles also use DISPATCH_LEVEL as a lock: on its single CPU another
// thread cannot enter one of these regions until the current thread lowers IRQL.
// Host threads run concurrently, so updating KPCR.Irql alone is insufficient.
class IrqlGate {
public:
    void Set(uint8_t& current, uint8_t next) {
        const bool entering = current < 2 && next >= 2;
        const bool leaving = current >= 2 && next < 2;
        if (entering) dispatch_.lock();
        current = next;
        if (leaving) dispatch_.unlock();
    }

private:
    std::mutex dispatch_;
};

}  // namespace kernel
