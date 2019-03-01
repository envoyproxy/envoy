#pragma once

#include "gtest/gtest.h"

namespace Envoy {

// TODO(jmarantz): Before Alyssa found this TestListener hook for me, we had
// merged a PR that added the same functionality via common TestBase classes.
// These should be removed after some grace period.
using TestBase = ::testing::Test;
template <class T> using TestBaseWithParam = ::testing::TestWithParam<T>;

} // namespace Envoy
