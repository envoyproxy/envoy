#include <functional>

#include "common/common/utility.h"
#include "common/event/dispatched_thread.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Event {

class DispatchedThreadTest : public testing::Test {
protected:
  DispatchedThreadTest()
      : config_(1000, 1000, 1000, 1000), guard_dog_(fakestats_, config_, time_source_) {}

  void SetUp() { thread_.start(guard_dog_); }
  NiceMock<Server::Configuration::MockMain> config_;
  NiceMock<Stats::MockStore> fakestats_;
  ProdMonotonicTimeSource time_source_;
  Envoy::Server::GuardDogImpl guard_dog_;
  DispatchedThreadImpl thread_;
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
