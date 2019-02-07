#pragma once

#include "gtest/gtest.h"

namespace Envoy {

class TestScope {
public:
  ~TestScope();

  // Ensures that there any test-scoped singletons created by instantiating
  // Test::Global<T> have been destroyed, which should occur after each
  // test method.
  static void checkSingletonQuiescensce();
};

// Provides a common test-base class for all tests in Envoy to use. This offers
// a place to put hooks we'd like to run on every test. There's currently a
// check that all test-scoped singletons have been destroyed. A test-scoped
// singleton might remain at the end of a test if it's transitively referenced
// by a leaked structure or a static.
//
// In the future, we can also add:
//   - a test-specific ThreadFactory that enables us to verify there are no
//     outstanding threads at the end of each test.
//   - a check that no more bytes of memory are allocated at the end of a test
//     than there were at the start of it. This is likely to fail in a few
//     places when introduced, but we could add known test overrides for this.
//
// Note: nothing compute-intensive should be put in this test-class, as it will
// be a tax paid by every test method in the codebase.
class TestBase : public ::testing::Test {
public:
 private:
  TestScope test_scope_;
};

// Templatized version of TestBase.
template <class T>
class TestBaseWithParam : public ::testing::TestWithParam<T> {
 private:
  TestScope test_scope_;
};

} // namespace Envoy
