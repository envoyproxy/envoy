#include <iostream>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

// NOLINT(namespace-envoy)
int main() {
#if defined(__linux__)
  // Exercise libc APIs missing from the GCC archive's bundled sysroot.
  const int fd = memfd_create("toolchain-detect", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    return 1;
  }
  const int result = fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
  close(fd);
  if (result < 0) {
    return 1;
  }
#endif

#if defined(__clang__)
  std::cout << "Compiler: clang\n";
  std::cout << "Compiler version: " << __clang_major__ << "." << __clang_minor__ << "."
            << __clang_patchlevel__ << "\n";
#elif defined(__GNUC__)
  std::cout << "Compiler: gcc\n";
  std::cout << "Compiler version: " << __GNUC__ << "." << __GNUC_MINOR__ << "."
            << __GNUC_PATCHLEVEL__ << "\n";
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

  std::cout << "GCC build: " << (ENVOY_TOOLCHAIN_GCC_BUILD ? "true" : "false") << "\n";

  return 0;
}
