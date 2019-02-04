#pragma once

#include "gtest/gtest.h"

namespace Envoy {

// Provides a common test-base class for all tests in Envoy to use. This offers
// a place to put hooks we'd like to run on every tests.
class TestBase : public ::testing::Test {
public:
  static bool checkSingletonQuiescensce();
  ~TestBase() override;
};

// Templatized version of TestBase.
template <class T> class TestBaseWithParam : public ::testing::TestWithParam<T> {
public:
  ~TestBaseWithParam() { TestBase::checkSingletonQuiescensce(); }
};

} // namespace Envoy
