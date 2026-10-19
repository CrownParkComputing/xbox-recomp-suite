#pragma once
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <string>

namespace logx {

// a release build writes Report and Error lines only, every line with XBE_LOG_VERBOSE=1
enum class Level { Debug, Info, Warn, Error, Report };

// release: dir/prefix.log, carried on when append; debug: dir/prefix_NNN.log per run, newest 30 kept
void Init(const std::string& dir, const std::string& prefix, bool append = false);
void Write(Level lv, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void Flush();
const std::string& Path();

// XBE_LOG_VERBOSE_DEFAULT: a release build that still writes every line, for a platform whose release build is
// the one being brought up (Android has no debug build to fall back to)
#if defined(XBE_LOG_VERBOSE_DEFAULT)
inline constexpr bool kVerbose = true;
#elif defined(NDEBUG)
inline constexpr bool kVerbose = false;
#else
inline constexpr bool kVerbose = true;
#endif

}  // namespace logx

#define LOGD(...) ::logx::Write(::logx::Level::Debug, __VA_ARGS__)
#define LOGI(...) ::logx::Write(::logx::Level::Info, __VA_ARGS__)
#define LOGW(...) ::logx::Write(::logx::Level::Warn, __VA_ARGS__)
#define LOGE(...) ::logx::Write(::logx::Level::Error, __VA_ARGS__)
#define LOGR(...) ::logx::Write(::logx::Level::Report, __VA_ARGS__)

// logs only the first n times a call site is reached
#define LOG_FIRST(n, ...)                                  \
    do {                                                   \
        static std::atomic<int> _c{0};                     \
        if (_c.fetch_add(1, std::memory_order_relaxed) < (n)) LOGI(__VA_ARGS__); \
    } while (0)
