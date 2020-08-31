#include "envoy/extensions/watchdog/profile_action/v3alpha/profile_action.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"

#include "extensions/watchdog/profile_action/config.h"

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
	      "@type": "type.googleapis.com/udpa.type.v1.TypedStruct",
	      "type_url": "type.googleapis.com/envoy.extensions.watchdog.profile_action.v3alpha.ProfileActionConfig",
	      "value": {
		"profile_duration": "2s",
		"profile_path": "/tmp/envoy/",
		"max_profiles_per_thread": "20"
	      }
            }
          },
        }
      )EOF",
      config);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher};

  EXPECT_CALL(dispatcher, createTimer_(_));
  EXPECT_NE(factory->createGuardDogActionFromProto(config, context), nullptr);
}

} // namespace
} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
