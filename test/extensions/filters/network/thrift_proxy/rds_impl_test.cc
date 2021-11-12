#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/config_dump.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

using ::Envoy::Matchers::UniversalStringMatcher;

class RouteConfigProviderManagerImplTest : public testing::Test {
public:
  RouteConfigProviderManagerImplTest() {
    // For server_factory_context
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));

    EXPECT_CALL(server_factory_context_.admin_.config_tracker_, add_("thrift_routes", _));
    route_config_provider_manager_ =
        std::make_unique<RouteConfigProviderManagerImpl>(server_factory_context_.admin_);
  }

  ~RouteConfigProviderManagerImplTest() override {
    server_factory_context_.thread_local_.shutdownThread();
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<Init::MockManager> outer_init_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Envoy::Config::SubscriptionCallbacks* rds_callbacks_{};
  NiceMock<Stats::MockIsolatedStatsStore> scope_;

  RouteConfigProviderManagerImplPtr route_config_provider_manager_;
};

TEST_F(RouteConfigProviderManagerImplTest, ConfigDump) {
  UniversalStringMatcher universal_name_matcher;
  auto message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["thrift_routes"](
          universal_name_matcher);
  const auto& route_config_dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);

  // No routes at all, no last_updated timestamp
  envoy::admin::v3::RoutesConfigDump expected_route_config_dump;
  TestUtility::loadFromYaml(R"EOF(
static_route_configs:
dynamic_route_configs:
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump.DebugString());

  const std::string config_yaml = R"EOF(
name: foo
routes:
  - match:
      method_name: bar
    route:
      cluster: baz
)EOF";

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  server_factory_context_.cluster_manager_.initializeClusters({"baz"}, {});
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(config_yaml, route_config);
  Rds::RouteConfigProviderPtr static_config =
      route_config_provider_manager_->createStaticRouteConfigProvider(route_config,
                                                                      server_factory_context_);

  envoy::extensions::filters::network::thrift_proxy::v3::Trds rds;
  rds.set_route_config_name("foo_route_config");
  rds.mutable_config_source()->set_path("foo_path");
  Rds::RouteConfigProviderSharedPtr dynamic_config =
      route_config_provider_manager_->createRdsRouteConfigProvider(
          rds, server_factory_context_, "foo_prefix.", outer_init_manager_);

  const std::string response1_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration",
      "name": "foo_route_config",
      "routes": [
        {
          "match":
          {
            "method_name": "baz"
          },
          "route":
          {
            "cluster": "baz"
          }
        }
      ]
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response1);

  rds_callbacks_ = server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  rds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info());
  message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["thrift_routes"](
          universal_name_matcher);
  const auto& route_config_dump3 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  TestUtility::loadFromYaml(R"EOF(
static_route_configs:
  - route_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
      name: foo
      routes:
        - match:
            method_name: bar
          route:
            cluster: baz
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_route_configs:
  - version_info: "1"
    route_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
      name: foo_route_config
      routes:
        - match:
            method_name: baz
          route:
            cluster: baz
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump3.DebugString());
}

} // namespace
} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
