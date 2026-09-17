#include "envoy/extensions/watchdog/backtrace_action/v3/backtrace_action.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"

#include "source/extensions/watchdog/backtrace_action/config.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {
namespace {

TEST(BacktraceActionFactoryTest, CanCreateAction) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::GuardDogActionFactory>::getFactory(
          "envoy.watchdog.backtrace_action");
  ASSERT_NE(factory, nullptr);

  envoy::config::bootstrap::v3::Watchdog::WatchdogAction config;
  TestUtility::loadFromJson(
      R"EOF(
        {
          "config": {
            "name": "envoy.watchdog.backtrace_action",
            "typed_config": {
              "@type": "type.googleapis.com/xds.type.v3.TypedStruct",
              "type_url": "type.googleapis.com/envoy.extensions.watchdog.backtrace_action.v3.BacktraceActionConfig",
              "value": {
                "cooldown_duration": "20s",
              }
            }
          },
        }
      )EOF",
      config);

  Stats::TestUtil::TestStore stats;
  Event::MockDispatcher dispatcher;
  EXPECT_CALL(dispatcher, createTimer_(testing::_))
      .Times(16) // Same value as MaxSlots. See backtrace_action.h.
      .WillRepeatedly(Invoke([](Event::TimerCb) { return new NiceMock<Event::MockTimer>(); }));
  Api::ApiPtr api = Api::createApiForTest(stats);
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher, *stats.rootScope(),
                                                              "test"};
  EXPECT_NE(factory->createGuardDogActionFromProto(config, context), nullptr);
}

} // namespace
} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
