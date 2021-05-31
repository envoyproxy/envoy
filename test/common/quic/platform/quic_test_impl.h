#pragma once

#include <string>

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

// TODO(mpwarres): implement once QUICHE flag mechanism is defined.
class QuicFlagSaverImpl {};

// No special setup needed for tests to use threads.
class ScopedEnvironmentForThreadsImpl {};

using QuicTestImpl = ::testing::Test;

template <class T> using QuicTestWithParamImpl = ::testing::TestWithParam<T>;

inline std::string QuicGetTestMemoryCachePathImpl() {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // TODO(mpwarres): implement
}
