#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace quiche {
namespace test {

using QuicheTest = ::testing::Test;

template <class T> using QuicheTestWithParamImpl = ::testing::TestWithParam<T>;

} // namespace test
} // namespace quiche
