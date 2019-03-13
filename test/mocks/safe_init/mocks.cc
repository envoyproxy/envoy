#include "test/mocks/safe_init/mocks.h"

namespace Envoy {
namespace SafeInit {

using ::testing::InvokeWithoutArgs;

MockWatcher::MockWatcher(absl::string_view name) : WatcherImpl(name, {[this]() { ready(); }}) {}
::testing::internal::TypedExpectation<void()>& MockWatcher::expectReady() const {
  return EXPECT_CALL(*this, ready());
}

MockTarget::MockTarget(absl::string_view name) : TargetImpl(name) {}
::testing::internal::TypedExpectation<void()>& MockTarget::expectInitialize() {
  return EXPECT_CALL(*this, initialize());
}
::testing::internal::TypedExpectation<void()>& MockTarget::expectInitializeWillCallReady() {
  return expectInitialize().WillOnce(InvokeWithoutArgs(this, &MockTarget::ready));
}

} // namespace SafeInit
} // namespace Envoy
