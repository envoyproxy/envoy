#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/init/manager.h"
#include "envoy/stats/scope.h"

#include "common/config/grpc_mux_impl.h"
#include "common/router/scoped_rds.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::IsNull;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::Http::TestHeaderMapImpl;

envoy::api::v2::ScopedRouteConfiguration
parseScopedRouteConfigurationFromYaml(const std::string& yaml) {
  envoy::api::v2::ScopedRouteConfiguration scoped_route_config;
  TestUtility::loadFromYaml(yaml, scoped_route_config);
  return scoped_route_config;
}

void parseScopedRouteConfigurationFromYaml(ProtobufWkt::Any& scoped_route_config,
                                           const std::string& yaml) {
  scoped_route_config.PackFrom(parseScopedRouteConfigurationFromYaml(yaml));
}

envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
parseHttpConnectionManagerFromYaml(const std::string& config_yaml) {
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
      http_connection_manager;
  TestUtility::loadFromYaml(config_yaml, http_connection_manager);
  return http_connection_manager;
}

class ScopedRoutesTestBase : public testing::Test {
protected:
  ScopedRoutesTestBase() {
    ON_CALL(factory_context_, initManager()).WillByDefault(ReturnRef(context_init_manager_));
    ON_CALL(factory_context_, getServerFactoryContext())
        .WillByDefault(ReturnRef(server_factory_context_));

    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_ =
        std::make_unique<RouteConfigProviderManagerImpl>(factory_context_.admin_);

    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("route_scopes", _));
    config_provider_manager_ = std::make_unique<ScopedRoutesConfigProviderManager>(
        factory_context_.admin_, *route_config_provider_manager_);
  }

  ~ScopedRoutesTestBase() override { factory_context_.thread_local_.shutdownThread(); }

  // The delta style API helper.
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource>
  anyToResource(Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                const std::string& version) {
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> added_resources;
    for (const auto& resource_any : resources) {
      auto config = TestUtility::anyConvert<envoy::api::v2::ScopedRouteConfiguration>(resource_any);
      auto* to_add = added_resources.Add();
      to_add->set_name(config.name());
      to_add->set_version(version);
      to_add->mutable_resource()->PackFrom(config);
    }
    return added_resources;
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  NiceMock<Init::MockManager> context_init_manager_;
  // factory_context_ is used by srds
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  // server_factory_context_ is used by rds
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  std::unique_ptr<RouteConfigProviderManager> route_config_provider_manager_;
  std::unique_ptr<ScopedRoutesConfigProviderManager> config_provider_manager_;

  Event::SimulatedTimeSystem time_system_;
};

class ScopedRdsTest : public ScopedRoutesTestBase {
protected:
  void setup() {
    ON_CALL(factory_context_.cluster_manager_, adsMux())
        .WillByDefault(Return(std::make_shared<::Envoy::Config::NullGrpcMuxImpl>()));

    InSequence s;
    // Since factory_context_.cluster_manager_.subscription_factory_.callbacks_ is taken by the SRDS
    // subscription. We need to return a different MockSubscription here for each RDS subscription.
    // To build the map from RDS route_config_name to the RDS subscription, we need to get the
    // route_config_name by mocking start() on the Config::Subscription.

    // srds subscription
    EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
                subscriptionFromConfigSource(_, _, _, _))
        .Times(AnyNumber());
    // rds subscription
    EXPECT_CALL(server_factory_context_.cluster_manager_.subscription_factory_,
                subscriptionFromConfigSource(
                    _,
                    Eq(Grpc::Common::typeUrl(
                        envoy::api::v2::RouteConfiguration().GetDescriptor()->full_name())),
                    _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](const envoy::api::v2::core::ConfigSource&, absl::string_view,
                                      Stats::Scope&,
                                      Envoy::Config::SubscriptionCallbacks& callbacks) {
          auto ret = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
          rds_subscription_by_config_subscription_[ret.get()] = &callbacks;
          EXPECT_CALL(*ret, start(_))
              .WillOnce(Invoke(
                  [this, config_sub_addr = ret.get()](const std::set<std::string>& resource_names) {
                    EXPECT_EQ(resource_names.size(), 1);
                    auto iter = rds_subscription_by_config_subscription_.find(config_sub_addr);
                    EXPECT_NE(iter, rds_subscription_by_config_subscription_.end());
                    rds_subscription_by_name_[*resource_names.begin()] = iter->second;
                  }));
          return ret;
        }));

    ON_CALL(context_init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      target_handles_.push_back(target.createHandle("test"));
    }));
    ON_CALL(context_init_manager_, initialize(_))
        .WillByDefault(Invoke([this](const Init::Watcher& watcher) {
          for (auto& handle_ : target_handles_) {
            handle_->initialize(watcher);
          }
        }));

    const std::string config_yaml = R"EOF(
name: foo_scoped_routes
scope_key_builder:
  fragments:
    - header_value_extractor:
        name: Addr
        element:
          key: x-foo-key
          separator: ;
)EOF";
    envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes scoped_routes_config;
    TestUtility::loadFromYaml(config_yaml, scoped_routes_config);
    provider_ = config_provider_manager_->createXdsConfigProvider(
        scoped_routes_config.scoped_rds(), factory_context_, "foo.",
        ScopedRoutesConfigProviderManagerOptArg(scoped_routes_config.name(),
                                                scoped_routes_config.rds_config_source(),
                                                scoped_routes_config.scope_key_builder()));
    srds_subscription_ = factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  // Helper function which pushes an update to given RDS subscription, the start(_) of the
  // subscription must have been called.
  void pushRdsConfig(const std::string& route_config_name, const std::string& version) {
    const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: test
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: bluh }}
)EOF";
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
    resources.Add()->PackFrom(TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(
        fmt::format(route_config_tmpl, route_config_name)));
    rds_subscription_by_name_[route_config_name]->onConfigUpdate(resources, version);
  }

  ScopedRdsConfigProvider* getScopedRdsProvider() const {
    return dynamic_cast<ScopedRdsConfigProvider*>(provider_.get());
  }
  // Helper function which returns the ScopedRouteMap of the subscription.
  const ScopedRouteMap& getScopedRouteMap() const {
    return getScopedRdsProvider()->subscription().scopedRouteMap();
  }

  Envoy::Config::SubscriptionCallbacks* srds_subscription_{};
  Envoy::Config::ConfigProviderPtr provider_;
  std::list<Init::TargetHandlePtr> target_handles_;
  Init::ExpectableWatcherImpl init_watcher_;

  // RDS mocks.
  absl::flat_hash_map<Envoy::Config::Subscription*, Envoy::Config::SubscriptionCallbacks*>
      rds_subscription_by_config_subscription_;
  absl::flat_hash_map<std::string, Envoy::Config::SubscriptionCallbacks*> rds_subscription_by_name_;
};

TEST_F(ScopedRdsTest, ValidateFail) {
  setup();

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
  EXPECT_THROW(srds_subscription_->onConfigUpdate(resources, "1"), ProtoValidationException);

  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(anyToResource(resources, "1"), {}, "1"), EnvoyException,
      "Error adding/updating scoped route\\(s\\): Proto constraint validation failed.*");

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
  EXPECT_THROW(srds_subscription_->onConfigUpdate(resources2, "1"), ProtoValidationException);
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(anyToResource(resources2, "1"), {}, "1"), EnvoyException,
      "Error adding/updating scoped route\\(s\\): Proto constraint validation failed.*");

  // 'key' validation: must define at least 1 fragment.
  const std::string config_yaml3 = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
)EOF";
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources3;
  parseScopedRouteConfigurationFromYaml(*resources3.Add(), config_yaml3);
  EXPECT_THROW(srds_subscription_->onConfigUpdate(resources3, "1"), ProtoValidationException);
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(anyToResource(resources3, "1"), {}, "1"), EnvoyException,
      "Error adding/updating scoped route\\(s\\): Proto constraint validation failed .*value is "
      "required.*");
}

// Tests that multiple uniquely named non-conflict resources are allowed in config updates.
TEST_F(ScopedRdsTest, MultipleResourcesSotw) {
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
    - string_key: x-bar-key
)EOF";
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml2);
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(resources, "1"));
  context_init_manager_.initialize(init_watcher_);
  init_watcher_.expectReady().Times(2); // SRDS and RDS "foo_routes"
  EXPECT_EQ(
      1UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());

  // Verify the config is a ScopedConfigImpl instance, both scopes point to "" as RDS hasn't kicked
  // in yet(NullConfigImpl returned).
  EXPECT_NE(getScopedRdsProvider(), nullptr);
  EXPECT_NE(getScopedRdsProvider()->config<ScopedConfigImpl>(), nullptr);
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "");
  // RDS updates foo_routes.
  pushRdsConfig("foo_routes", "111");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "foo_routes");

  // Delete foo_scope2.
  resources.RemoveLast();
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(resources, "3"));
  EXPECT_EQ(getScopedRouteMap().size(), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
  EXPECT_EQ(
      2UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  // now scope key "x-bar-key" points to nowhere.
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}}),
              IsNull());
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that multiple uniquely named non-conflict resources are allowed in config updates.
TEST_F(ScopedRdsTest, MultipleResourcesDelta) {
  setup();
  init_watcher_.expectReady().Times(2); // SRDS and RDS "foo_routes"

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
    - string_key: x-bar-key
)EOF";
  parseScopedRouteConfigurationFromYaml(*resources.Add(), config_yaml2);

  // Delta API.
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(anyToResource(resources, "2"), {}, "1"));
  context_init_manager_.initialize(init_watcher_);
  EXPECT_EQ(
      1UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);

  // Verify the config is a ScopedConfigImpl instance, both scopes point to "" as RDS hasn't kicked
  // in yet(NullConfigImpl returned).
  EXPECT_NE(getScopedRdsProvider(), nullptr);
  EXPECT_NE(getScopedRdsProvider()->config<ScopedConfigImpl>(), nullptr);
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "");
  // RDS updates foo_routes.
  pushRdsConfig("foo_routes", "111");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "foo_routes");

  // Delete foo_scope2.
  resources.RemoveLast();
  Protobuf::RepeatedPtrField<std::string> deletes;
  *deletes.Add() = "foo_scope2";
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(anyToResource(resources, "4"), deletes, "2"));
  EXPECT_EQ(getScopedRouteMap().size(), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
  EXPECT_EQ(
      2UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  // now scope key "x-bar-key" points to nowhere.
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}}),
              IsNull());
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that conflict resources are detected.
TEST_F(ScopedRdsTest, MultipleResourcesWithKeyConflict) {
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
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(resources, "1"), EnvoyException,
      ".*scope key conflict found, first scope is 'foo_scope', second scope is 'foo_scope2'");
  EXPECT_EQ(
      // Fully rejected.
      0UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  // Scope key "x-foo-key" points to nowhere.
  EXPECT_NE(getScopedRdsProvider(), nullptr);
  EXPECT_NE(getScopedRdsProvider()->config<ScopedConfigImpl>(), nullptr);
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}}),
              IsNull());
  context_init_manager_.initialize(init_watcher_);
  init_watcher_.expectReady().Times(
      1); // Just SRDS, RDS "foo_routes" will initialized by the noop init-manager.
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value(),
            0UL);

  // Delta API.
  EXPECT_CALL(context_init_manager_, state()).WillOnce(Return(Init::Manager::State::Initialized));
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(anyToResource(resources, "2"), {}, "2"), EnvoyException,
      ".*scope key conflict found, first scope is 'foo_scope', second scope is 'foo_scope2'");
  EXPECT_EQ(
      // Partially reject.
      1UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  // foo_scope update is applied.
  EXPECT_EQ(getScopedRouteMap().size(), 1UL);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
  // Scope key "x-foo-key" points to foo_routes due to partial rejection.
  pushRdsConfig("foo_routes", "111"); // Push some real route configuration.
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value());
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that only one resource is provided during a config update.
TEST_F(ScopedRdsTest, InvalidDuplicateResourceSotw) {
  setup();
  context_init_manager_.initialize(init_watcher_);
  init_watcher_.expectReady().Times(0);

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
  EXPECT_THROW_WITH_MESSAGE(srds_subscription_->onConfigUpdate(resources, "1"), EnvoyException,
                            "duplicate scoped route configuration 'foo_scope' found");
}

// Tests that only one resource is provided during a config update.
TEST_F(ScopedRdsTest, InvalidDuplicateResourceDelta) {
  setup();
  context_init_manager_.initialize(init_watcher_);
  // After the above initialize, the default init_manager should return "Initialized".
  EXPECT_CALL(context_init_manager_, state()).WillOnce(Return(Init::Manager::State::Initialized));
  init_watcher_.expectReady().Times(
      1); // SRDS onConfigUpdate breaks, but first foo_routes will
          // kick start if it's initialized post-Server/LDS initialization.

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
  EXPECT_THROW_WITH_MESSAGE(
      srds_subscription_->onConfigUpdate(anyToResource(resources, "1"), {}, "1"), EnvoyException,
      "Error adding/updating scoped route(s): duplicate scoped route configuration 'foo_scope' "
      "found");
  EXPECT_EQ(
      // Partially reject.
      1UL,
      factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload").value());
  // foo_scope update is applied.
  EXPECT_EQ(getScopedRouteMap().size(), 1UL);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
}

// Tests a config update failure.
TEST_F(ScopedRdsTest, ConfigUpdateFailure) {
  setup();

  const auto time = std::chrono::milliseconds(1234567891234);
  timeSystem().setSystemTime(time);
  const EnvoyException ex(fmt::format("config failure"));
  // Verify the failure updates the lastUpdated() timestamp.
  srds_subscription_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                           &ex);
  EXPECT_EQ(std::chrono::time_point_cast<std::chrono::milliseconds>(provider_->lastUpdated())
                .time_since_epoch(),
            time);
}

// Tests that the /config_dump handler returns the corresponding scoped routing
// config.
TEST_F(ScopedRdsTest, ConfigDump) {
  setup();
  context_init_manager_.initialize(init_watcher_);
  EXPECT_CALL(context_init_manager_, state())
      .Times(2) // There are two SRDS pushes.
      .WillRepeatedly(Return(Init::Manager::State::Initialized));
  init_watcher_.expectReady().Times(1); // SRDS only, no RDS push.

  auto message_ptr =
      factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump =
      TestUtility::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);

  // No routes at all(no SRDS push yet), no last_updated timestamp
  envoy::admin::v2alpha::ScopedRoutesConfigDump expected_config_dump;
  TestUtility::loadFromYaml(R"EOF(
inline_scoped_route_configs:
dynamic_scoped_route_configs:
)EOF",
                            expected_config_dump);
  EXPECT_TRUE(TestUtility::protoEqual(expected_config_dump, scoped_routes_config_dump));

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  const std::string hcm_base_config_yaml = R"EOF(
codec_type: auto
stat_prefix: foo
http_filters:
  - name: http_dynamo_filter
    config:
scoped_routes:
  name: $0
  scope_key_builder:
    fragments:
      - header_value_extractor:
          name: Addr
          index: 0
$1
)EOF";
  const std::string inline_scoped_route_configs_yaml = R"EOF(
  scoped_route_configurations_list:
    scoped_route_configurations:
      - name: foo
        route_configuration_name: foo-route-config
        key:
          fragments: { string_key: "172.10.10.10" }
      - name: foo2
        route_configuration_name: foo-route-config2
        key:
          fragments: { string_key: "172.10.10.20" }
)EOF";
  // Only load the inline scopes.
  Envoy::Config::ConfigProviderPtr inline_config = ScopedRoutesConfigProviderUtil::create(
      parseHttpConnectionManagerFromYaml(absl::Substitute(hcm_base_config_yaml, "foo-scoped-routes",
                                                          inline_scoped_route_configs_yaml)),
      factory_context_, "foo.", *config_provider_manager_);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump2 =
      TestUtility::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  TestUtility::loadFromYaml(R"EOF(
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
  EXPECT_TRUE(TestUtility::protoEqual(expected_config_dump, scoped_routes_config_dump2));

  // Now SRDS kicks off.
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(parseScopedRouteConfigurationFromYaml(R"EOF(
name: dynamic-foo
route_configuration_name: dynamic-foo-route-config
key:
  fragments: { string_key: "172.30.30.10" }
)EOF"));

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891567));
  srds_subscription_->onConfigUpdate(resources, "1");

  TestUtility::loadFromYaml(R"EOF(
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
  - name: foo_scoped_routes
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
      TestUtility::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_TRUE(TestUtility::protoEqual(expected_config_dump, scoped_routes_config_dump3));

  resources.Clear();
  srds_subscription_->onConfigUpdate(resources, "2");
  TestUtility::loadFromYaml(R"EOF(
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
  - name: foo_scoped_routes
    last_updated:
      seconds: 1234567891
      nanos: 567000000
    version_info: "2"
)EOF",
                            expected_config_dump);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump4 =
      TestUtility::downcastAndValidate<const envoy::admin::v2alpha::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_TRUE(TestUtility::protoEqual(expected_config_dump, scoped_routes_config_dump4));
}

// Tests that SRDS only allows creation of delta static config providers.
TEST_F(ScopedRdsTest, DeltaStaticConfigProviderOnly) {
  // Use match all regex due to lack of distinctive matchable output for
  // coverage test.
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
