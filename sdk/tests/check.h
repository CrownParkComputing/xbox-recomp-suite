#pragma once
// A small test runner: TEST(name) { ... } registers a case; CHECK keeps going on failure, REQUIRE stops the case.
// Failures print the expression, the values when given, and where.
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    std::function<void()> run;
};
std::vector<Case>& Cases();
struct Register {
    Register(const char* name, std::function<void()> run) { Cases().push_back({name, std::move(run)}); }
};
struct Stop {};  // thrown by REQUIRE
void Fail(const char* file, int line, const std::string& what);
int Failures();

template <typename A, typename B>
std::string Pair(const A& a, const B& b) {
    std::ostringstream s;
    s << a << " vs " << b;
    return s.str();
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                                       \
    static void CHECK_CAT(test_, __LINE__)();                                            \
    static check::Register CHECK_CAT(reg_, __LINE__)(name, CHECK_CAT(test_, __LINE__)); \
    static void CHECK_CAT(test_, __LINE__)()
#define CHECK(expr) \
    do {            \
        if (!(expr)) check::Fail(__FILE__, __LINE__, #expr); \
    } while (0)
#define CHECK_EQ(a, b)                                                                                \
    do {                                                                                              \
        auto&& ca_ = (a);                                                                             \
        auto&& cb_ = (b);                                                                             \
        if (!(ca_ == cb_)) check::Fail(__FILE__, __LINE__, std::string(#a " == " #b ": ") + check::Pair(ca_, cb_)); \
    } while (0)
#define CHECK_MSG(expr, msg)                                          \
    do {                                                              \
        if (!(expr)) check::Fail(__FILE__, __LINE__, std::string(#expr ": ") + (msg)); \
    } while (0)
#define REQUIRE(expr)                                         \
    do {                                                      \
        if (!(expr)) {                                        \
            check::Fail(__FILE__, __LINE__, #expr);           \
            throw check::Stop{};                              \
        }                                                     \
    } while (0)
