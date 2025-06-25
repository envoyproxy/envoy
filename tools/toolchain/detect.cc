#include <iostream>

// NOLINT(namespace-envoy)
int main() {
#if defined(__clang__)
  std::cout << "Compiler: clang\n";
#elif defined(__GNUC__)
  std::cout << "Compiler: gcc\n";
#else
  std::cout << "Compiler: unknown\n";
#endif

#if defined(_LIBCPP_VERSION)
  std::cout << "Standard Library: libc++\n";
#elif defined(__GLIBCXX__)
  std::cout << "Standard Library: libstdc++\n";
#else
  std::cout << "Standard Library: unknown\n";
#endif

  return 0;
}
