#include <functional>

#include "common/api/api_impl.h"
#include "common/common/utility.h"
#include "common/event/dispatched_thread.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Event {

class DispatchedThreadTest : public testing::Test {
protected:
  DispatchedThreadTest()
      : config_(1000, 1000, 1000, 1000), api_(std::chrono::milliseconds(1000)),
        thread_(api_, test_time_.timeSystem()),
        guard_dog_(fakestats_, config_, test_time_.timeSystem(), api_) {}

  void SetUp() { thread_.start(guard_dog_); }
  NiceMock<Server::Configuration::MockMain> config_;
  NiceMock<Stats::MockStore> fakestats_;
  DangerousDeprecatedTestTime test_time_;
  Api::Impl api_;
  DispatchedThreadImpl thread_;
  Envoy::Server::GuardDogImpl guard_dog_;
};

TEST_F(DispatchedThreadTest, PostCallbackTest) {
  InSequence s;
  ReadyWatcher watcher;

  EXPECT_CALL(watcher, ready());
  thread_.dispatcher().post([&watcher]() { watcher.ready(); });

  thread_.exit();
}

} // namespace Event
} // namespace Envoy
