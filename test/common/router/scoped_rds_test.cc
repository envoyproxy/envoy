#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/router/scoped_rds.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

envoy::api::v2::ScopedRouteConfiguration
parseScopedRouteConfigurationFromYaml(const std::string& yaml) {
  envoy::api::v2::ScopedRouteConfiguration scoped_route_config;
  MessageUtil::loadFromYaml(yaml, scoped_route_config);
  return scoped_route_config;
}

void parseScopedRouteConfigurationFromYaml(ProtobufWkt::Any& scoped_route_config,
                                           const std::string& yaml) {
  scoped_route_config.PackFrom(parseScopedRouteConfigurationFromYaml(yaml));
}

std::vector<std::unique_ptr<const Protobuf::Message>>
protosToMessageVec(std::vector<envoy::api::v2::ScopedRouteConfiguration>&& protos) {
  std::vector<std::unique_ptr<const Protobuf::Message>> messages;
  for (const auto& proto : protos) {
    Protobuf::Message* message = proto.New();
    message->CopyFrom(proto);
    messages.push_back(std::unique_ptr<const Protobuf::Message>(message));
  }
  return messages;
}

class ScopedRoutesTestBase : public testing::Test {
protected:
  ScopedRoutesTestBase() {
    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("route_scopes", _));
    config_provider_manager_ =
        std::make_unique<ScopedRoutesConfigProviderManager>(factory_context_.admin_);

    const std::string rds_config_yaml = R"EOF(
api_config_source:
  api_type: REST
  cluster_names:
    - foo_rds_cluster
  refresh_delay: { seconds: 1, nanos: 0 }
)EOF";
    MessageUtil::loadFromYaml(rds_config_yaml, rds_config_source_);
  }

  ~ScopedRoutesTestBase() override { factory_context_.thread_local_.shutdownThread(); }

  void setupMockClusterMap() {
    InSequence s;
    cluster_map_.emplace("foo_cluster", cluster_);
    EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map_));
    EXPECT_CALL(cluster_, info());
    EXPECT_CALL(*cluster_.info_, addedViaApi());
    EXPECT_CALL(cluster_, info());
    EXPECT_CALL(*cluster_.info_, type());
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;
  Upstream::MockClusterMockPrioritySet cluster_;
  std::unique_ptr<ScopedRoutesConfigProviderManager> config_provider_manager_;
  Event::SimulatedTimeSystem time_system_;
  envoy::api::v2::core::ConfigSource rds_config_source_;
};

class ScopedRdsTest : public ScopedRoutesTestBase {
protected:
  void setup() {
    InSequence s;

    setupMockClusterMap();
    const std::string config_yaml = R"EOF(
name: foo_scoped_routes
scope_key_builder:
  fragments:
    - header_value_extractor: { name: X-Google-VIP }
rds_config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
scoped_rds:
  scoped_rds_config_source:
    api_config_source:
      api_type: REST
      cluster_names:
        - foo_cluster
      refresh_delay: { seconds: 1, nanos: 0 }
)EOF";
    envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes scoped_routes_config;
    MessageUtil::loadFromYaml(config_yaml, scoped_routes_config);
    provider_ = config_provider_manager_->createXdsConfigProvider(
        scoped_routes_config.scoped_rds(), factory_context_, "foo.",
        ScopedRoutesConfigProviderManagerOptArg(scoped_routes_config.name(),
                                                scoped_routes_config.rds_config_source(),
                                                scoped_routes_config.scope_key_builder()));
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
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml);
  EXPECT_THROW(subscription.onConfigUpdate(resources, "1"), ProtoValidationException);

  // 'route_configuration_name' validation: value must be > 1 byte.
  const std::string config_yaml2 = R"EOF(
name: foo_scope
route_configuration_name:
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources2;
  parseScopedRouteConfigurationFromYaml(*resources2.Add(), config_yaml2);
  EXPECT_THROW(subscription.onConfigUpdate(resources2, "1"), ProtoValidationException);

  // 'key' validation: must define at least 1 fragment.
  const std::string config_yaml3 = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources3;
  parseScopedRouteConfigurationFromYaml(*resources3.Add(), config_yaml3);
  EXPECT_THROW(subscription.onConfigUpdate(resources3, "1"), ProtoValidationException);
}

// Tests that multiple uniquely named resources are allowed in config updates.
TEST_F(ScopedRdsTest, MultipleResources) {
  setup();

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml);
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml2);
  EXPECT_NO_THROW(subscription().onConfigUpdate(resources, "1"));
  EXPECT_EQ(
      1UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
}

// Tests that only one resource is provided during a config update.
TEST_F(ScopedRdsTest, InvalidDuplicateResource) {
  setup();

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml);
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml);
  EXPECT_THROW_WITH_MESSAGE(subscription().onConfigUpdate(resources, "1"), EnvoyException,
                            "duplicate scoped route configuration foo_scope found");
}

// Tests that defining an invalid cluster in the SRDS config results in an error.
TEST_F(ScopedRdsTest, UnknownCluster) {
  const std::string config_yaml = R"EOF(
name: foo_scoped_routes
scope_key_builder:
  fragments:
    - header_value_extractor: { name: X-Google-VIP }
rds_config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
scoped_rds:
  scoped_rds_config_source:
    api_config_source:
      api_type: REST
      cluster_names:
        - foo_cluster
      refresh_delay: { seconds: 1, nanos: 0 }
)EOF";
  envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes scoped_routes_config;
  MessageUtil::loadFromYaml(config_yaml, scoped_routes_config);

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      config_provider_manager_->createXdsConfigProvider(
          scoped_routes_config.scoped_rds(), factory_context_, "foo.",
          ScopedRoutesConfigProviderManagerOptArg(scoped_routes_config.name(),
                                                  scoped_routes_config.rds_config_source(),
                                                  scoped_routes_config.scope_key_builder())),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS "
      "cluster: 'foo_cluster' does not exist, was added via api, or is an "
      "EDS cluster");
}

// Tests a config update failure.
TEST_F(ScopedRdsTest, ConfigUpdateFailure) {
  setup();

  const auto time = std::chrono::milliseconds(1234567891234);
  timeSystem().setSystemTime(time);
  const EnvoyException ex(fmt::format("config failure"));
  // Verify the failure updates the lastUpdated() timestamp.
  subscription().onConfigUpdateFailed(&ex);
  EXPECT_EQ(std::chrono::time_point_cast<std::chrono::milliseconds>(provider_->lastUpdated())
                .time_since_epoch(),
            time);
}

using ScopedRoutesConfigProviderManagerTest = ScopedRoutesTestBase;

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
inline_scoped_route_configs:
dynamic_scoped_route_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump.DebugString());

  const std::string config_yaml = R"EOF(
name: foo
route_configuration_name: foo-route-config
key:
  fragments: { string_key: "172.10.10.10" }
)EOF";
  const std::string config_yaml2 = R"EOF(
name: foo2
route_configuration_name: foo-route-config2
key:
  fragments: { string_key: "172.10.10.20" }
)EOF";
  std::vector<std::unique_ptr<const Protobuf::Message>> config_protos =
      protosToMessageVec({parseScopedRouteConfigurationFromYaml(config_yaml),
                          parseScopedRouteConfigurationFromYaml(config_yaml2)});

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  envoy::config::filter::network::http_connection_manager::v2 ::ScopedRoutes::ScopeKeyBuilder
      scope_key_builder;
  MessageUtil::loadFromYaml(R"EOF(
fragments:
  - header_value_extractor: { name: X-Google-VIP }
)EOF",
                            scope_key_builder);
  // Only load the inline scopes.
  Envoy::Config::ConfigProviderPtr inline_config =
      config_provider_manager_->createStaticConfigProvider(
          std::move(config_protos), factory_context_,
          ScopedRoutesConfigProviderManagerOptArg("foo-scoped-routes", rds_config_source_,
                                                  scope_key_builder));
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump2 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       route_configuration_name: foo-route-config2
       key:
         fragments: { string_key: "172.10.10.20" }
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_scoped_route_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump2.DebugString());

  setupMockClusterMap();
  envoy::config::filter::network::http_connection_manager::v2::ScopedRds scoped_rds_config;
  const std::string config_source_yaml = R"EOF(
scoped_rds_config_source:
  api_config_source:
    api_type: REST
    cluster_names:
      - foo_cluster
    refresh_delay: { seconds: 1, nanos: 0 }
)EOF";
  MessageUtil::loadFromYaml(config_source_yaml, scoped_rds_config);
  Envoy::Config::ConfigProviderPtr dynamic_provider =
      config_provider_manager_->createXdsConfigProvider(
          scoped_rds_config, factory_context_, "foo.",
          ScopedRoutesConfigProviderManagerOptArg("foo-dynamic-scoped-routes", rds_config_source_,
                                                  scope_key_builder));

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(parseScopedRouteConfigurationFromYaml(R"EOF(
name: dynamic-foo
route_configuration_name: dynamic-foo-route-config
key:
  fragments: { string_key: "172.30.30.10" }
)EOF"));

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891567));
  ScopedRdsConfigSubscription& subscription =
      dynamic_cast<ScopedRdsConfigProvider&>(*dynamic_provider).subscription();
  subscription.onConfigUpdate(resources, "1");

  MessageUtil::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       route_configuration_name: foo-route-config2
       key:
         fragments: { string_key: "172.10.10.20" }
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_scoped_route_configs:
  - name: foo-dynamic-scoped-routes
    scoped_route_configs:
      - name: dynamic-foo
        route_configuration_name: dynamic-foo-route-config
        key:
          fragments: { string_key: "172.30.30.10" }
    last_updated:
      seconds: 1234567891
      nanos: 567000000
    version_info: "1"
)EOF",
                            expected_config_dump);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump3 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump3.DebugString());

  resources.Clear();
  subscription.onConfigUpdate(resources, "2");
  MessageUtil::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       route_configuration_name: foo-route-config2
       key:
         fragments: { string_key: "172.10.10.20" }
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_scoped_route_configs:
  - name: foo-dynamic-scoped-routes
    last_updated:
      seconds: 1234567891
      nanos: 567000000
    version_info: "2"
)EOF",
                            expected_config_dump);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump4 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_EQ(expected_config_dump.DebugString(), scoped_routes_config_dump4.DebugString());
}

using ScopedRoutesConfigProviderManagerDeathTest = ScopedRoutesConfigProviderManagerTest;

// Tests that SRDS only allows creation of delta static config providers.
TEST_F(ScopedRoutesConfigProviderManagerDeathTest, DeltaStaticConfigProviderOnly) {
  // Use match all regex due to lack of distinctive matchable output for coverage test.
  EXPECT_DEATH(config_provider_manager_->createStaticConfigProvider(
                   parseScopedRouteConfigurationFromYaml(R"EOF(
name: dynamic-foo
route_configuration_name: static-foo-route-config
key:
  fragments: { string_key: "172.30.30.10" }
)EOF"),
                   factory_context_, Envoy::Config::ConfigProviderManager::NullOptionalArg()),
               ".*");
}

} // namespace
} // namespace Router
} // namespace Envoy
