#include "envoy/extensions/watchdog/profile_action/v3/profile_action.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"

#include "source/extensions/watchdog/profile_action/config.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {
namespace {

TEST(ProfileActionFactoryTest, CanCreateAction) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::GuardDogActionFactory>::getFactory(
          "envoy.watchdog.profile_action");
  ASSERT_NE(factory, nullptr);

  // Create config and mock context
  envoy::config::bootstrap::v3::Watchdog::WatchdogAction config;
  TestUtility::loadFromJson(
      R"EOF(
        {
          "config": {
            "name": "envoy.watchdog.profile_action",
            "typed_config": {
              "@type": "type.googleapis.com/xds.type.v3.TypedStruct",
              "type_url": "type.googleapis.com/envoy.extensions.watchdog.profile_action.v3.ProfileActionConfig",
              "value": {
                "profile_duration": "2s",
                "profile_path": "/tmp/envoy/",
                "max_profiles": "20"
              }
            }
          },
        }
      )EOF",
      config);

  Stats::TestUtil::TestStore stats;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest(stats);
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher, *stats.rootScope(),
                                                              "test"};

  EXPECT_CALL(dispatcher, createTimer_(_));
  EXPECT_NE(factory->createGuardDogActionFromProto(config, context), nullptr);
}

} // namespace
} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
