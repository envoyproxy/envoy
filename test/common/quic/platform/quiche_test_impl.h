#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace quiche {
namespace test {

using QuicheTest = ::testing::Test;

template <class T> using QuicheTestWithParamImpl = ::testing::TestWithParam<T>;

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string QuicheGetCommonSourcePathImpl() {
  std::string test_srcdir(getenv("TEST_SRCDIR"));
  return absl::StrCat(test_srcdir, "/external/com_googlesource_quiche/quiche/common");
}

} // namespace test
} // namespace quiche
