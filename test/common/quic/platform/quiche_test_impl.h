#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "source/common/common/assert.h"

#include "absl/flags/reflection.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/common/platform/api/quiche_flags.h"

using QuicheFlagSaverImpl = absl::FlagSaver;

// No special setup needed for tests to use threads.
class ScopedEnvironmentForThreadsImpl {};

inline std::string QuicheGetTestMemoryCachePathImpl() { // NOLINT(readability-identifier-naming)
  PANIC("not implemented");                             // TODO(mpwarres): implement
}

namespace quiche {
namespace test {

using QuicheTestImpl = ::testing::Test;
using QuicTestImpl = QuicheTestImpl;

template <class T> using QuicheTestWithParamImpl = ::testing::TestWithParam<T>;
template <class T> using QuicTestWithParamImpl = QuicheTestWithParamImpl<T>;

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string QuicheGetCommonSourcePathImpl() {
  std::string test_srcdir(getenv("TEST_SRCDIR"));
  return absl::StrCat(test_srcdir, "/external/com_github_google_quiche/quiche/common");
}

} // namespace test
} // namespace quiche
