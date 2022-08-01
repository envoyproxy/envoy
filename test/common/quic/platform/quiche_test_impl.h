#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/common/platform/api/quiche_flags.h"

class QuicheFlagSaverImpl {
public:
  QuicheFlagSaverImpl() {
    // Save the current value of each flag.
#define QUIC_PROTOCOL_FLAG(type, flag, ...) saved_##flag##_ = GetQuicheFlagImpl(FLAGS_##flag);
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define QUIC_FLAG(flag, ...) saved_##flag##_ = GetQuicheFlagImpl(FLAGS_##flag);
#include "quiche/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
  }

  ~QuicheFlagSaverImpl() {
    // Restore the saved value of each flag.
#define QUIC_PROTOCOL_FLAG(type, flag, ...) SetQuicheFlagImpl(FLAGS_##flag, saved_##flag##_);
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define QUIC_FLAG(flag, ...) SetQuicheFlagImpl(FLAGS_##flag, saved_##flag##_);
#include "quiche/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
  }

private:
  // Local variable will hold the value of each flag when the saver is constructed.
#define QUIC_PROTOCOL_FLAG(type, flag, ...) type saved_##flag##_;
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#define QUIC_FLAG(flag, ...) bool saved_##flag##_;
#include "quiche/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
};

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
