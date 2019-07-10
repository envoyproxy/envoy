#include "test/mocks/init/mocks.h"

namespace Envoy {
namespace Init {

using ::testing::Invoke;

ExpectableWatcherImpl::ExpectableWatcherImpl(absl::string_view name)
    : WatcherImpl(name, {[this]() { ready(); }}) {}
::testing::internal::TypedExpectation<void()>& ExpectableWatcherImpl::expectReady() const {
  return EXPECT_CALL(*this, ready());
}

ExpectableTargetImpl::ExpectableTargetImpl(absl::string_view name)
    : TargetImpl(name, {[this]() { initialize(); }}) {}
::testing::internal::TypedExpectation<void()>& ExpectableTargetImpl::expectInitialize() {
  return EXPECT_CALL(*this, initialize());
}
::testing::internal::TypedExpectation<void()>&
ExpectableTargetImpl::expectInitializeWillCallReady() {
  return expectInitialize().WillOnce(Invoke([this]() { ready(); }));
}

ExpectableEagerTargetImpl::ExpectableEagerTargetImpl(absl::string_view name)
    : EagerTargetImpl(name, {[this]() { initialize(); }}) {}
::testing::internal::TypedExpectation<void()>& ExpectableEagerTargetImpl::expectInitialize() {
  return EXPECT_CALL(*this, initialize());
}
::testing::internal::TypedExpectation<void()>&
ExpectableEagerTargetImpl::expectInitializeWillCallReady() {
  return expectInitialize().WillOnce(Invoke([this]() { ready(); }));
}
} // namespace Init
} // namespace Envoy
