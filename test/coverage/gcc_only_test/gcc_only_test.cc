#include "gtest/gtest.h"

namespace Envoy {

TEST(GccOnly, CompilerCheck) {
#if defined(__clang__) or not defined(__GNUC__)
  // clang is incompatible with gcov.
  FAIL() << "GCC is required for coverage runs";
#endif
}

} // namespace Envoy
