// sundog host tests: tiny CHECK macros. On failure: print details, exit 1.
#ifndef SUNDOG_TESTS_CHECK_H
#define SUNDOG_TESTS_CHECK_H

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_checks = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond,          \
                   __FILE__, __LINE__);                                      \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

#define CHECK_MSG(cond, ...)                                                 \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n  ", #cond,        \
                   __FILE__, __LINE__);                                      \
      std::fprintf(stderr, __VA_ARGS__);                                     \
      std::fprintf(stderr, "\n");                                            \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
  do {                                                                       \
    ++g_checks;                                                              \
    double _a = (double)(a), _b = (double)(b), _e = (double)(eps);           \
    if (!(std::fabs(_a - _b) <= _e)) {                                       \
      std::fprintf(stderr,                                                   \
                   "CHECK_NEAR failed: %s = %.9g vs %s = %.9g (|diff| = "    \
                   "%.3g > eps = %.3g)\n  at %s:%d\n",                       \
                   #a, _a, #b, _b, std::fabs(_a - _b), _e, __FILE__,         \
                   __LINE__);                                                \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

#define TEST_DONE(name)                                                      \
  do {                                                                       \
    std::printf("%s: %d checks passed\n", name, g_checks);                   \
    return 0;                                                                \
  } while (0)

#endif
