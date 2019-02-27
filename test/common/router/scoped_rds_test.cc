#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/router/scoped_rds.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_base.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

void parseScopedRoutesConfigFromYaml(
    envoy::api::v2::ScopedRouteConfigurationsSet& scoped_routes_proto, const std::string& yaml) {
  MessageUtil::loadFromYaml(yaml, scoped_routes_proto);
}

envoy::api::v2::ScopedRouteConfigurationsSet
parseScopedRoutesConfigFromYaml(const std::string& yaml) {
  envoy::api::v2::ScopedRouteConfigurationsSet scoped_routes_proto;
  parseScopedRoutesConfigFromYaml(scoped_routes_proto, yaml);
  return scoped_routes_proto;
}

class ScopedRoutesTestBase : public TestBase {
protected:
  ScopedRoutesTestBase() {
    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("route_scopes", _));
    config_provider_manager_ =
        std::make_unique<ScopedRoutesConfigProviderManager>(factory_context_.admin_);

    const std::string rds_config_yaml = R"EOF(
config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_rds_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
scoped_rds_template: true
    )EOF";
    MessageUtil::loadFromYaml(rds_config_yaml, rds_config_);
  }

  ~ScopedRoutesTestBase() override { factory_context_.thread_local_.shutdownThread(); }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<ScopedRoutesConfigProviderManager> config_provider_manager_;
  Event::SimulatedTimeSystem time_system_;
  envoy::config::filter::network::http_connection_manager::v2::Rds rds_config_;
};

class ScopedRdsTest : public ScopedRoutesTestBase {
protected:
  ScopedRdsTest() = default;

  ~ScopedRdsTest() override = default;

  void setup() {
    InSequence s;

    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, type());

    const std::string config_yaml = R"EOF(
config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
scoped_routes_config_set_name: foo_scope_set
    )EOF";
    envoy::config::filter::network::http_connection_manager::v2::ScopedRds scoped_rds_config;
    MessageUtil::loadFromYaml(config_yaml, scoped_rds_config);
    provider_ = config_provider_manager_->createXdsConfigProvider(
        scoped_rds_config, factory_context_, "foo.",
        ScopedRoutesConfigProviderManagerOptArg(rds_config_));
    subscription_ = &dynamic_cast<ScopedRdsConfigProvider&>(*provider_).subscription();
  }

  ScopedRdsConfigSubscription& subscription() const { return *subscription_; }

  ScopedRdsConfigSubscription* subscription_;
  Envoy::Config::ConfigProviderPtr provider_;
};

TEST_F(ScopedRdsTest, ValidateFail) {
  setup();

  ScopedRdsConfigSubscription& subscription =
      dynamic_cast<ScopedRdsConfigProvider&>(*provider_).subscription();

  // 'name' validation: value must be > 1 byte.
  const std::string config_yaml = R"EOF(
name:
scope_key_builder:
  fragments:
    - header_value_extractor: { name: element }
scopes:
  - route_configuration_name: foo_routes
    key:
      fragments:
        - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources;
  parseScopedRoutesConfigFromYaml(*resources.Add(), config_yaml);
  EXPECT_THROW(subscription.onConfigUpdate(resources, "1"), ProtoValidationException);

  // 'scope_key_builder.fragments' validation: must define at least 1 item in the repeated field.
  const std::string config_yaml2 = R"EOF(
name: foo_scope_set
scope_key_builder:
  fragments:
scopes:
  - route_configuration_name: foo_routes
    key:
      fragments:
        - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources2;
  parseScopedRoutesConfigFromYaml(*resources2.Add(), config_yaml2);
  EXPECT_THROW(subscription.onConfigUpdate(resources2, "1"), ProtoValidationException);

  // 'scopes.fragments' validation: must define at least 1 item in the repeated field.
  const std::string config_yaml3 = R"EOF(
name: foo_scope_set
scope_key_builder:
  fragments:
scopes:
  - route_configuration_name: foo_routes
    key:
)EOF";
  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources3;
  parseScopedRoutesConfigFromYaml(*resources3.Add(), config_yaml3);
  EXPECT_THROW(subscription.onConfigUpdate(resources3, "1"), ProtoValidationException);
}

// Tests that an empty config update will update the corresponding stat.
TEST_F(ScopedRdsTest, EmptyResource) {
  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources;
  subscription().onConfigUpdate(resources, "1");
  EXPECT_EQ(1UL,
            factory_context_.scope_.counter("foo.scoped_rds.foo_scope_set.update_empty").value());
}

// Tests that only one resource is provided during a config update.
TEST_F(ScopedRdsTest, TooManyResources) {
  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources;
  resources.Add();
  resources.Add();
  EXPECT_THROW(subscription().onConfigUpdate(resources, "1"), EnvoyException);
}

TEST_F(ScopedRdsTest, ConfigUpdateSuccess) {
  setup();

  ScopedRdsConfigSubscription& subscription =
      dynamic_cast<ScopedRdsConfigProvider&>(*provider_).subscription();

  // 'name' validation
  const std::string config_yaml = R"EOF(
name: foo_scope_set
scope_key_builder:
  fragments:
    - header_value_extractor: { name: element }
scopes:
  - route_configuration_name: foo_routes
    key:
      fragments:
        - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<envoy::api::v2::ScopedRouteConfigurationsSet> resources;
  parseScopedRoutesConfigFromYaml(*resources.Add(), config_yaml);
  subscription.onConfigUpdate(resources, "1");
  EXPECT_EQ(1UL,
            factory_context_.scope_.counter("foo.scoped_rds.foo_scope_set.config_reload").value());
}

// Tests that defining an invalid cluster in the SRDS config results in an error.
TEST_F(ScopedRdsTest, UnknownCluster) {
  const std::string config_yaml = R"EOF(
config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
scoped_routes_config_set_name: foo_scope_set
    )EOF";
  envoy::config::filter::network::http_connection_manager::v2::ScopedRds scoped_rds_config;
  MessageUtil::loadFromYaml(config_yaml, scoped_rds_config);

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      config_provider_manager_->createXdsConfigProvider(
          scoped_rds_config, factory_context_, "foo.",
          ScopedRoutesConfigProviderManagerOptArg(rds_config_));
      , EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS "
      "cluster: 'foo_cluster' does not exist, was added via api, or is an "
      "EDS cluster");
}

class ScopedRoutesConfigProviderManagerTest : public ScopedRoutesTestBase {
public:
  ScopedRoutesConfigProviderManagerTest() = default;

  ~ScopedRoutesConfigProviderManagerTest() override = default;
};

// Tests that the /config_dump handler returns the corresponding scoped routing config.
TEST_F(ScopedRoutesConfigProviderManagerTest, ConfigDump) {
  auto message_ptr =
      factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);

  // No routes at all, no last_updated timestamp
  envoy::admin::v2alpha::ScopedRoutesConfigDump expected_config_dump;
  MessageUtil::loadFromYaml(R"EOF(
inline_scoped_routes_configs:
dynamic_scoped_routes_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump.DebugString());

  std::string config_yaml = R"EOF(
name: foo
scope_key_builder:
  fragments:
    - header_value_extractor: { name: X-Google-VIP }
scopes:
  - route_configuration_name: foo-route-config
    key:
      fragments: { string_key: "172.10.10.10" }
)EOF";

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  // Only load the inline scopes.
  Envoy::Config::ConfigProviderPtr inline_config =
      config_provider_manager_->createStaticConfigProvider(
          parseScopedRoutesConfigFromYaml(config_yaml), factory_context_,
          ScopedRoutesConfigProviderManagerOptArg(rds_config_));
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump2 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
inline_scoped_routes_configs:
  - scoped_routes_config:
      name: foo
      scope_key_builder:
        fragments:
          - header_value_extractor: { name: X-Google-VIP }
      scopes:
        - route_configuration_name: foo-route-config
          key:
            fragments: { string_key: "172.10.10.10" }
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_scoped_routes_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump2.DebugString());
}

} // namespace
} // namespace Router
} // namespace Envoy
