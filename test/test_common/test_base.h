#pragma once

#include "gtest/gtest.h"

namespace Envoy {

class TestBaseScope {
public:
  ~TestBaseScope();
};

// Provides a common test-base class for all tests in Envoy to use. This offers
// a place to put hooks we'd like to run on every tests.
class TestBase : public ::testing::Test, public TestBaseScope {
public:
  static bool checkSingletonQuiescensce();
};

// Templatized version of TestBase.
template <class T>
class TestBaseWithParam : public ::testing::TestWithParam<T>, public TestBaseScope {
public:
};

#define TEST_E(fixture, name, code)                                                                \
  TEST(fixture, name) {                                                                            \
    TestBaseScope test_base_scope;                                                                 \
    code                                                                                           \
  }

} // namespace Envoy
