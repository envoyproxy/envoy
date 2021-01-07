#include "envoy/event/dispatcher.h"

#include "common/api/api_impl.h"
#include "common/common/scope_tracker.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(ScopeTrackerScopeStateTest, ShouldManageTrackedObjectOnDispatcherStack) {
  Api::ApiPtr api_(Api::createApiForTest());
  Event::DispatcherPtr dispatcher_(api_->allocateDispatcher("test_thread"));
  MockScopedTrackedObject tracked_object;

  // Nothing should be tracked so far.
  dispatcher_->popTrackedObject(nullptr);

  {
    ScopeTrackerScopeState scope(&tracked_object, *dispatcher_);
    // Check that the tracked_object is on the tracked object stack
    dispatcher_->popTrackedObject(&tracked_object);

    // Restore it to the top, it should be removed in the dtor of scope.
    dispatcher_->pushTrackedObject(&tracked_object);
  }

  // Nothing should be tracked now.
  dispatcher_->popTrackedObject(nullptr);
}

} // namespace
} // namespace Envoy
