#pragma once
#include <mutex>

namespace kernel {

// Opt-in single guest CPU. Ownership belongs to a host thread; nested guest
// callbacks share it, and a kernel call can temporarily give it up.
class GuestCpu {
public:
    void Enable() { enabled_ = true; }  // before starting guest threads
    bool Enabled() const { return enabled_; }
    bool Enter() {
        if (!enabled_ || owner_ == this) return false;
        mutex_.lock();
        owner_ = this;
        return true;
    }
    bool Leave() {
        if (owner_ != this) return false;
        owner_ = nullptr;
        mutex_.unlock();
        return true;
    }
private:
    bool enabled_ = false;
    std::mutex mutex_;
    inline static thread_local GuestCpu* owner_ = nullptr;
};

}  // namespace kernel
