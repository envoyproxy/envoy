#include "source/common/common/cancel_wrapper.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace CancelWrapper {

TEST(CancelWrapper, WrappedCallbackIsExecutable) {
  int x = 0;
  absl::AnyInvocable<void()> cancel;
  absl::AnyInvocable<void()> cb = [&x]() { x = 1; };
  auto wrapped = cancelWrapped(std::move(cb), &cancel);
  EXPECT_EQ(0, x);
  wrapped();
  EXPECT_EQ(1, x);
}

TEST(CancelWrapper, WrappedCallbackBareLambdaIsExecutable) {
  int x = 0;
  absl::AnyInvocable<void()> cancel;
  auto wrapped = cancelWrapped([&x]() { x = 1; }, &cancel);
  EXPECT_EQ(0, x);
  wrapped();
  EXPECT_EQ(1, x);
}

TEST(CancelWrapper, CancelledCallbackIsExecutableButDoesNothing) {
  int x = 0;
  absl::AnyInvocable<void()> cb = [&x]() { x = 1; };
  absl::AnyInvocable<void()> cancel;
  auto wrapped = cancelWrapped(std::move(cb), &cancel);
  cancel();
  EXPECT_EQ(0, x);
  wrapped();
  EXPECT_EQ(0, x);
}

TEST(CancelWrapper, WrappedCallbackWithArgsIsExecutable) {
  int x = 0;
  absl::AnyInvocable<void(int)> cb = [&x](int new_val) { x = new_val; };
  absl::AnyInvocable<void()> cancel;
  auto wrapped = cancelWrapped(std::move(cb), &cancel);
  EXPECT_EQ(0, x);
  wrapped(3);
  EXPECT_EQ(3, x);
}

TEST(CancelWrapper, WrappedCallbackWithNonCopyableArgsAndCapturesIsExecutable) {
  int x = 0;
  absl::AnyInvocable<void(std::unique_ptr<int>)> cb = [y = std::make_unique<int>(5),
                                                       &x](std::unique_ptr<int> added_val) mutable {
    x = *y + *added_val;
  };
  absl::AnyInvocable<void()> cancel;
  auto wrapped = cancelWrapped(std::move(cb), &cancel);
  EXPECT_EQ(0, x);
  wrapped(std::make_unique<int>(3));
  EXPECT_EQ(8, x);
}

TEST(CancelWrapper, WrappedCallbackLambdaWithNonCopyableArgsAndCapturesIsExecutable) {
  int x = 0;
  absl::AnyInvocable<void()> cancel;
  auto wrapped = cancelWrapped([y = std::make_unique<int>(5), &x](
                                   std::unique_ptr<int> added_val) mutable { x = *y + *added_val; },
                               &cancel);
  EXPECT_EQ(0, x);
  wrapped(std::make_unique<int>(3));
  EXPECT_EQ(8, x);
}

} // namespace CancelWrapper
} // namespace Envoy
