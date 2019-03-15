#include "test/mocks/safe_init/mocks.h"

namespace Envoy {
namespace SafeInit {

using ::testing::Invoke;

MockWatcher::MockWatcher(absl::string_view name) : WatcherImpl(name, {[this]() { ready(); }}) {}
::testing::internal::TypedExpectation<void()>& MockWatcher::expectReady() const {
  return EXPECT_CALL(*this, ready());
}

MockTarget::MockTarget(absl::string_view name) : TargetImpl(name) {}
::testing::internal::TypedExpectation<void()>& MockTarget::expectInitialize() {
  return EXPECT_CALL(*this, initialize());
}
::testing::internal::TypedExpectation<void()>& MockTarget::expectInitializeWillCallReady() {
  return expectInitialize().WillOnce(Invoke([this]() { ready(); }));
}

} // namespace SafeInit
} // namespace Envoy
