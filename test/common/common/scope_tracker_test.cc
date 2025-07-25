#include <iostream>

#include "source/common/api/api_impl.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/event/dispatcher_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using testing::_;

TEST(ScopeTrackerScopeStateTest, ShouldManageTrackedObjectOnDispatcherStack) {
  Api::ApiPtr api(Api::createApiForTest());
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  MockScopeTrackedObject tracked_object;
  {
    ScopeTrackerScopeState scope(&tracked_object, *dispatcher);
    // Check that the tracked_object is on the tracked object stack
    dispatcher->popTrackedObject(&tracked_object);

    // Restore it to the top, it should be removed in the dtor of scope.
    dispatcher->pushTrackedObject(&tracked_object);
  }

  // Check nothing is tracked now.
  EXPECT_CALL(tracked_object, dumpState(_, _)).Times(0);
  static_cast<Event::DispatcherImpl*>(dispatcher.get())->onFatalError(std::cerr);
}

} // namespace Envoy
