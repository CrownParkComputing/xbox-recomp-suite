// runs every case, or those whose names contain the first argument
#include <cstring>

#include "check.h"

namespace check {

namespace {
int g_failures = 0;
bool g_case_failed = false;
}  // namespace

std::vector<Case>& Cases() {
    static std::vector<Case> cases;
    return cases;
}

void Fail(const char* file, int line, const std::string& what) {
    std::printf("    FAIL %s:%d: %s\n", file, line, what.c_str());
    g_failures++;
    g_case_failed = true;
}

int Failures() { return g_failures; }

}  // namespace check

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const check::Case& c : check::Cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        check::g_case_failed = false;
        std::printf("%s\n", c.name);
        try {
            c.run();
        } catch (const check::Stop&) {
        }
        run++;
        failed += check::g_case_failed;
    }
    std::printf("\n%d cases, %d failed, %d failed checks\n", run, failed, check::Failures());
    return failed ? 1 : 0;
}
