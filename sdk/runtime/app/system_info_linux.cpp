// the log's opening lines on Linux: what the game runs on and with which settings (system_info.cpp on Windows)
#include <sys/utsname.h>
#include <unistd.h>

#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"

namespace app {

namespace {

// a "key: value" or "KEY=value" line's value from a file, empty without it
std::string Field(const char* path, const std::string& key, char sep) {
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        if (line.compare(0, key.size(), key) != 0) continue;
        size_t at = line.find(sep, key.size());
        if (at == std::string::npos) continue;
        std::string v = line.substr(at + 1);
        v.erase(0, v.find_first_not_of(" \t\""));
        v.erase(v.find_last_not_of(" \t\"") + 1);
        return v;
    }
    return {};
}

std::string Narrow(const wchar_t* w) {
    std::string out;
    for (; *w; w++) out += *w < 0x80 ? char(*w) : '?';
    return out;
}

}  // namespace

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%s (%s), %s", Narrow(kGame.title).c_str(), IMAGE_XBE, date);
    LOGR("Path: %s", ExeDir().string().c_str());
    utsname u{};
    uname(&u);
    std::string os = Field("/etc/os-release", "PRETTY_NAME", '=');
    LOGR("OS: %s (%s %s)", os.empty() ? "Linux" : os.c_str(), u.sysname, u.release);
    std::string cpu = Field("/proc/cpuinfo", "model name", ':');
    LOGR("CPU: %s (%u threads)", cpu.empty() ? "unknown" : cpu.c_str(), std::thread::hardware_concurrency());
    LOGR("RAM: %.1f GB", double(sysconf(_SC_PHYS_PAGES)) * double(sysconf(_SC_PAGE_SIZE)) / 1073741824.0);
    Mode desk = Desktop();
    LOGR("Desktop: %ux%u", desk.width, desk.height);
    LogSettings();
}

}  // namespace app
