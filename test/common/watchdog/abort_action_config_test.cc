#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/watchdog/v3/abort_action.pb.h"

#include "source/common/watchdog/abort_action_config.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Watchdog {
namespace {

class AbortActionFactoryTest : public ::testing::TestWithParam<const char*> {};

INSTANTIATE_TEST_SUITE_P(TypedStruct, AbortActionFactoryTest,
                         ::testing::Values("xds.type.v3.TypedStruct", "udpa.type.v1.TypedStruct"));

TEST_P(AbortActionFactoryTest, CanCreateAction) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::GuardDogActionFactory>::getFactory(
          "envoy.watchdog.abort_action");
  ASSERT_NE(factory, nullptr);

  // Create config and mock context
  envoy::config::bootstrap::v3::Watchdog::WatchdogAction config;
  TestUtility::loadFromJson(absl::StrFormat(
                                R"EOF(
        {
          "config": {
            "name": "envoy.watchdog.abort_action",
            "typed_config": {
              "@type": "type.googleapis.com/%s",
              "type_url": "type.googleapis.com/envoy.watchdog.abort_action.v3.AbortActionConfig",
              "value": {
                "wait_duration": "2s",
              }
            }
          },
        }
      )EOF",
                                GetParam()),
                            config);

  Stats::TestUtil::TestStore stats_;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::Configuration::GuardDogActionFactoryContext context{*api, dispatcher, stats_, "test"};

  EXPECT_NE(factory->createGuardDogActionFromProto(config, context), nullptr);
}

} // namespace
} // namespace Watchdog
} // namespace Envoy
