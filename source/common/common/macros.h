#pragma once

namespace Envoy {

/**
 * @return the size of a C array.
 */
#define ARRAY_SIZE(X) (sizeof(X) / sizeof(X[0]))

/**
 * @return the length of a static string literal, e.g. STATIC_STRLEN("foo") == 3.
 */
#define STATIC_STRLEN(X) (sizeof(X) - 1)

/**
 * Helper macros from enum to string macros.
 */
#define GENERATE_ENUM(X) X,
#define GENERATE_STRING(X) #X,

/**
 * Stop the compiler from complaining about an unreferenced parameter.
 */
#ifndef WIN32
#define UNREFERENCED_PARAMETER(X) ((void)(X))
#endif

/**
 * Construct On First Use idiom.
 * See https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use.
 */
#define CONSTRUCT_ON_FIRST_USE(type, ...)                                                          \
  do {                                                                                             \
    static const type* objectptr = new type{__VA_ARGS__};                                          \
    return *objectptr;                                                                             \
  } while (0)

#define MUTABLE_CONSTRUCT_ON_FIRST_USE(type, ...)                                                  \
  do {                                                                                             \
    static type* objectptr = new type{__VA_ARGS__};                                                \
    return *objectptr;                                                                             \
  } while (0)

/**
 * Have a generic fall-through for different versions of C++
 */
#if __cplusplus >= 201703L // C++17 and above
#define FALLTHRU [[fallthrough]]
#elif __cplusplus >= 201402L && __clang_major__ >= 5 // C++14 clang-5
#define FALLTHRU [[fallthrough]]
#elif __cplusplus >= 201103L && __GNUC__ >= 7 // C++11 gcc 7
#define FALLTHRU [[gnu::fallthrough]]
#else // C++11 on gcc 6, and all other cases
#define FALLTHRU
#endif

} // namespace Envoy
