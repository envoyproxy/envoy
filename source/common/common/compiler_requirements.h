#pragma once

namespace Envoy {

#if __cplusplus < 201402L
#error "Your compiler does not support C++14. GCC 5+ or Clang is required."
#endif

// See:
//   https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
//   https://bugzilla.redhat.com/show_bug.cgi?id=1546704
#if defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI != 1 &&                              \
    !defined(ENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR)
#error "Your toolchain has set _GLIBCXX_USE_CXX11_ABI to a value that uses a std::string "         \
  "implementation that is not thread-safe. This may cause rare and difficult-to-debug errors "     \
  "if std::string is passed between threads in any way. If you accept this risk, you may define "  \
  "ENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1 in your build."
#endif

} // Envoy
