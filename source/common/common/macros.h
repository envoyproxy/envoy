#pragma once

namespace Envoy {
/**
 * @return the size of a C array.
 */
#define ARRAY_SIZE(X) (sizeof(X) / sizeof(X[0]))

/**
 * Helper macros from enum to string macros.
 */
#define GENERATE_ENUM(X) X,
#define GENERATE_STRING(X) #X,

/**
 * Stop the compiler from complaining about an unreferenced parameter.
 */
#define UNREFERENCED_PARAMETER(X) ((void)(X))

/**
 * Construct On First Use idiom.
 * See https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use.
 */
#define CONSTRUCT_ON_FIRST_USE(type, ...)                                                          \
  static const type* objectptr = new type{__VA_ARGS__};                                            \
  return *objectptr;

/**
 * Have a generic fall-through for different versions of C++
 */
#if __cplusplus >= 201402L // C++14, C++17 and above
#define FALLTHRU [[fallthrough]]
#elif __cplusplus >= 201103L && __GNUC__ >= 7 // C++11 gcc 7
#define FALLTHRU [[gnu::fallthrough]]
#else // C++11 on gcc 6, and all other cases
#define FALLTHRU
#endif
} // Envoy
