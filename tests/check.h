// A minimal test runner: TEST(name) { CHECK(cond); CHECK_NEAR(a, b, eps); }
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

struct TestCase {
    const char *name;
    std::function<void()> fn;
};
inline std::vector<TestCase> &test_cases() {
    static std::vector<TestCase> v;
    return v;
}
inline int &test_failures() {
    static int n = 0;
    return n;
}

struct TestRegistrar {
    TestRegistrar(const char *name, std::function<void()> fn) { test_cases().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                                                                                     \
    static void test_##name();                                                                                         \
    static TestRegistrar registrar_##name(#name, test_##name);                                                         \
    static void test_##name()

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            test_failures()++;                                                                                         \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);                                   \
        }                                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do {                                                                                                               \
        double a_ = (a), b_ = (b);                                                                                     \
        if (!(std::fabs(a_ - b_) <= (eps))) {                                                                          \
            test_failures()++;                                                                                         \
            fprintf(stderr, "%s:%d: CHECK_NEAR(%s, %s): %g vs %g\n", __FILE__, __LINE__, #a, #b, a_, b_);              \
        }                                                                                                              \
    } while (0)
