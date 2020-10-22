#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

class UserSpaceFileEventImplTest : public testing::Test {
public:
  UserSpaceFileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

protected:
  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};
} // namespace
} // namespace Event
} // namespace Envoy