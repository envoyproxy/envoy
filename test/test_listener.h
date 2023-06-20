#pragma once

#include "absl/flags/reflection.h"
#include "gtest/gtest.h"

namespace Envoy {

// Provides a test listener to be called after each test method. This offers
// a place to put hooks we'd like to run on every test. There's currently a
// check that all test-scoped singletons have been destroyed. A test-scoped
// singleton might remain at the end of a test if it's transitively referenced
// by a leaked structure or a static.
//
// In the future, we can also add:
//   - a test-specific ThreadFactory that enables us to verify there are no
//     outstanding threads at the end of each thread.
//   - a check that no more bytes of memory are allocated at the end of a test
//     than there were at the start of it. This is likely to fail in a few
//     places when introduced, but we could add known test overrides for this.
//
// Note: nothing compute-intensive should be put in this class, as it will
// be a tax paid by every test method in the codebase.
class TestListener : public ::testing::EmptyTestEventListener {
public:
  TestListener(bool validate_singletons = true)
      : saver_(std::make_unique<absl::FlagSaver>()), validate_singletons_(validate_singletons) {}
  void OnTestEnd(const ::testing::TestInfo& test_info) override;

private:
  // Make sure runtime guards are restored to defaults on test completion.
  std::unique_ptr<absl::FlagSaver> saver_;
  bool validate_singletons_;
};

} // namespace Envoy
