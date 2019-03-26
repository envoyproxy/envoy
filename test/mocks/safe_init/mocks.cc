#include "test/mocks/safe_init/mocks.h"

namespace Envoy {
namespace SafeInit {

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

} // namespace SafeInit
} // namespace Envoy
