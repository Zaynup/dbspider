

#include <assert.h>
#include <stdlib.h>
#include <string>
#include "util.h"

#if defined(__GNUC__) || defined(__llvm__)
#define DBSPIDER_LIKELY(x) __builtin_expect(!!(x), 1)
#define DBSPIDER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define DBSPIDER_LIKELY(x) (x)
#define DBSPIDER_UNLIKELY(x) (x)
#endif

#define DBSPIDER_ASSERT(x)                                                                      \
    if (DBSPIDER_UNLIKELY(!(x)))                                                                \
    {                                                                                           \
        DBSPIDER_LOG_ERROR(DBSPIDER_LOG_ROOT()) << "ASSERTION: " << #x                          \
                                                << "\nbacktrace:\n"                             \
                                                << dbspider::BacktraceToString(100, 2, "    "); \
        assert(x);                                                                              \
        exit(1);                                                                                \
    }

#define DBSPIDER_ASSERT2(x, m)                                                                  \
    if (DBSPIDER_UNLIKELY(!(x)))                                                                \
    {                                                                                           \
        DBSPIDER_LOG_ERROR(DBSPIDER_LOG_ROOT()) << "ASSERTION: " << #x                          \
                                                << "\n"                                         \
                                                << m << "\n"                                    \
                                                << "\nbacktrace:\n"                             \
                                                << dbspider::BacktraceToString(100, 2, "    "); \
        assert(x);                                                                              \
        exit(1);                                                                                \
    }

#define DBSPIDER_STATIC_ASSERT(x)                                                               \
    if constexpr (!(x))                                                                         \
    {                                                                                           \
        DBSPIDER_LOG_ERROR(DBSPIDER_LOG_ROOT()) << "ASSERTION: " << #x                          \
                                                << "\nbacktrace:\n"                             \
                                                << dbspider::BacktraceToString(100, 2, "    "); \
        exit(1);                                                                                \
    }
