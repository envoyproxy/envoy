#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/config_dump.pb.validate.h"
#include "envoy/api/v2/route.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/init/manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/api_version.h"
#include "common/config/grpc_mux_impl.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/router/scoped_rds.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/instance.h"
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
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::Http::TestRequestHeaderMapImpl;

envoy::config::route::v3::ScopedRouteConfiguration
parseScopedRouteConfigurationFromYaml(const std::string& yaml) {
  envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config;
  TestUtility::loadFromYaml(yaml, scoped_route_config, true);
  return scoped_route_config;
}

envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
parseHttpConnectionManagerFromYaml(const std::string& config_yaml) {
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      http_connection_manager;
  TestUtility::loadFromYaml(config_yaml, http_connection_manager, true);
  return http_connection_manager;
}

class ScopedRoutesTestBase : public testing::Test {
protected:
  ScopedRoutesTestBase() {
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    EXPECT_CALL(validation_context_, dynamicValidationVisitor())
        .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

    EXPECT_CALL(server_factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_ =
        std::make_unique<RouteConfigProviderManagerImpl>(server_factory_context_.admin_);

    EXPECT_CALL(server_factory_context_.admin_.config_tracker_, add_("route_scopes", _));
    config_provider_manager_ = std::make_unique<ScopedRoutesConfigProviderManager>(
        server_factory_context_.admin_, *route_config_provider_manager_);
  }

  ~ScopedRoutesTestBase() override { server_factory_context_.thread_local_.shutdownThread(); }

  // The delta style API helper.
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
  anyToResource(Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                const std::string& version) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
    for (const auto& resource_any : resources) {
      auto config =
          TestUtility::anyConvert<envoy::config::route::v3::ScopedRouteConfiguration>(resource_any);
      auto* to_add = added_resources.Add();
      to_add->set_name(config.name());
      to_add->set_version(version);
      to_add->mutable_resource()->PackFrom(config);
    }
    return added_resources;
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  NiceMock<Init::MockManager> context_init_manager_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  // server_factory_context_ is used by rds
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  RouteConfigProviderManagerPtr route_config_provider_manager_;
  ScopedRoutesConfigProviderManagerPtr config_provider_manager_;

  Event::SimulatedTimeSystem time_system_;
};

class ScopedRdsTest : public ScopedRoutesTestBase {
protected:
  void setup() {
    ON_CALL(server_factory_context_.cluster_manager_, adsMux())
        .WillByDefault(Return(std::make_shared<::Envoy::Config::NullGrpcMuxImpl>()));

    InSequence s;
    // Since server_factory_context_.cluster_manager_.subscription_factory_.callbacks_ is taken by
    // the SRDS subscription. We need to return a different MockSubscription here for each RDS
    // subscription. To build the map from RDS route_config_name to the RDS subscription, we need to
    // get the route_config_name by mocking start() on the Config::Subscription.

    // srds subscription
    EXPECT_CALL(server_factory_context_.cluster_manager_.subscription_factory_,
                subscriptionFromConfigSource(_, _, _, _, _))
        .Times(AnyNumber());
    // rds subscription
    EXPECT_CALL(
        server_factory_context_.cluster_manager_.subscription_factory_,
        subscriptionFromConfigSource(
            _,
            Eq(Grpc::Common::typeUrl(
                API_NO_BOOST(envoy::api::v2::RouteConfiguration)().GetDescriptor()->full_name())),
            _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](const envoy::config::core::v3::ConfigSource&,
                                      absl::string_view, Stats::Scope&,
                                      Envoy::Config::SubscriptionCallbacks& callbacks,
                                      Envoy::Config::OpaqueResourceDecoder&) {
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
    envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes
        scoped_routes_config;
    TestUtility::loadFromYaml(config_yaml, scoped_routes_config);
    provider_ = config_provider_manager_->createXdsConfigProvider(
        scoped_routes_config.scoped_rds(), server_factory_context_, context_init_manager_, "foo.",
        ScopedRoutesConfigProviderManagerOptArg(scoped_routes_config.name(),
                                                scoped_routes_config.rds_config_source(),
                                                scoped_routes_config.scope_key_builder()));
    srds_subscription_ = server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  // Helper function which pushes an update to given RDS subscription, the start(_) of the
  // subscription must have been called.
  void pushRdsConfig(const std::vector<std::string>& route_config_names,
                     const std::string& version) {
    const std::string route_config_tmpl = R"EOF(
      name: {}
      virtual_hosts:
      - name: test
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: bluh }}
)EOF";
    for (const std::string& name : route_config_names) {
      const auto route_config =
          TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(
              fmt::format(route_config_tmpl, name));
      const auto decoded_resources = TestUtility::decodeResources({route_config});
      rds_subscription_by_name_[name]->onConfigUpdate(decoded_resources.refvec_, version);
    }
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
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  init_watcher_.expectReady(); // Only the SRDS parent_init_target_.
  context_init_manager_.initialize(init_watcher_);
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"));
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());

  // Verify the config is a ScopedConfigImpl instance, both scopes point to "" as RDS hasn't kicked
  // in yet(NullConfigImpl returned).
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "");
  // RDS updates foo_routes.
  pushRdsConfig({"foo_routes"}, "111");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "foo_routes");

  // Delete foo_scope2.
  const auto decoded_resources_2 = TestUtility::decodeResources({resource});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_2.refvec_, "3"));
  EXPECT_EQ(getScopedRouteMap().size(), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
  EXPECT_EQ(2UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  // now scope key "x-bar-key" points to nowhere.
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}}),
              IsNull());
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that multiple uniquely named non-conflict resources are allowed in config updates.
TEST_F(ScopedRdsTest, MultipleResourcesDelta) {
  setup();
  init_watcher_.expectReady();
  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);

  // Delta API.
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  context_init_manager_.initialize(init_watcher_);
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, {}, "1"));
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);

  // Verify the config is a ScopedConfigImpl instance, both scopes point to "" as RDS hasn't kicked
  // in yet(NullConfigImpl returned).
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "");
  // RDS updates foo_routes.
  pushRdsConfig({"foo_routes"}, "111");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "foo_routes");

  // Delete foo_scope2.
  Protobuf::RepeatedPtrField<std::string> deletes;
  *deletes.Add() = "foo_scope2";
  const auto decoded_resources_2 = TestUtility::decodeResources({resource});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_2.refvec_, deletes, "2"));
  EXPECT_EQ(getScopedRouteMap().size(), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope"), 1);
  EXPECT_EQ(2UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  // now scope key "x-bar-key" points to nowhere.
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}}),
              IsNull());
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that conflict resources in the same push are detected.
TEST_F(ScopedRdsTest, MultipleResourcesWithKeyConflictSotW) {
  setup();

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  init_watcher_.expectReady().Times(0); // The onConfigUpdate will simply throw an exception.
  context_init_manager_.initialize(init_watcher_);
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"), EnvoyException,
      ".*scope key conflict found, first scope is 'foo_scope', second scope is 'foo_scope2'");
  EXPECT_EQ(
      // Fully rejected.
      0UL, server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
               .value());
  // Scope key "x-foo-key" points to nowhere.
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}}),
              IsNull());
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value(),
            0UL);
}

// Tests that conflict resources in the same push are detected in delta api form.
TEST_F(ScopedRdsTest, MultipleResourcesWithKeyConflictDelta) {
  setup();

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  init_watcher_.expectReady().Times(0); // The onConfigUpdate will simply throw an exception.
  context_init_manager_.initialize(init_watcher_);

  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"), EnvoyException,
      ".*scope key conflict found, first scope is 'foo_scope', second scope is 'foo_scope2'");
  EXPECT_EQ(
      // Fully rejected.
      0UL, server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
               .value());
  // Scope key "x-foo-key" points to nowhere.
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}}),
              IsNull());
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value(),
            0UL);
}

// Tests that scope-key conflict resources in different config updates are handled correctly.
TEST_F(ScopedRdsTest, ScopeKeyReuseInDifferentPushes) {
  setup();

  const std::string config_yaml1 = R"EOF(
name: foo_scope1
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const std::string config_yaml2 = R"EOF(
name: foo_scope2
route_configuration_name: bar_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml1);
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  init_watcher_.expectReady();
  context_init_manager_.initialize(init_watcher_);
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"));
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  // Scope key "x-foo-key" points to nowhere.
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  // No RDS "foo_routes" config push happened yet, Router::NullConfig is returned.
  EXPECT_THAT(getScopedRdsProvider()
                  ->config<ScopedConfigImpl>()
                  ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                  ->name(),
              "");
  pushRdsConfig({"foo_routes", "bar_routes"}, "111");
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value(),
            1UL);
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.bar_routes.config_reload").value(),
            1UL);
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");

  const std::string config_yaml3 = R"EOF(
name: foo_scope3
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";

  // Remove foo_scope1 and add a new scope3 reuses the same scope_key.
  const auto resource_3 = parseScopedRouteConfigurationFromYaml(config_yaml3);
  const auto decoded_resources_2 = TestUtility::decodeResources({resource_2, resource_3});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_2.refvec_, "2"));
  EXPECT_EQ(2UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  // foo_scope is deleted, and foo_scope2 is added.
  EXPECT_EQ(getScopedRouteMap().size(), 2UL);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope1"), 0);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope2"), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope3"), 1);
  // The same scope-key now points to the same route table.
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");

  // Push a new scope foo_scope4 with the same key as foo_scope2 but a different route-table, this
  // ends in an exception.
  const std::string config_yaml4 = R"EOF(
name: foo_scope4
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_4 = parseScopedRouteConfigurationFromYaml(config_yaml4);
  const auto decoded_resources_3 =
      TestUtility::decodeResources({resource_2, resource_3, resource_4});
  EXPECT_THROW_WITH_REGEX(
      srds_subscription_->onConfigUpdate(decoded_resources_3.refvec_, "3"), EnvoyException,
      "scope key conflict found, first scope is 'foo_scope2', second scope is 'foo_scope4'");
  EXPECT_EQ(getScopedRouteMap().size(), 2UL);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope1"), 0);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope2"), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope3"), 1);
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "bar_routes");

  // Delete foo_scope2, and push a new foo_scope4 with the same scope key but different route-table.
  const auto decoded_resources_4 = TestUtility::decodeResources({resource_3, resource_4});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_4.refvec_, "4"));
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value(),
            3UL);
  EXPECT_EQ(getScopedRouteMap().size(), 2UL);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope3"), 1);
  EXPECT_EQ(getScopedRouteMap().count("foo_scope4"), 1);
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-bar-key"}})
                ->name(),
            "foo_routes");
  EXPECT_EQ(getScopedRdsProvider()
                ->config<ScopedConfigImpl>()
                ->getRouteConfig(TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}})
                ->name(),
            "foo_routes");
}

// Tests that only one resource is provided during a config update.
TEST_F(ScopedRdsTest, InvalidDuplicateResourceSotw) {
  setup();
  init_watcher_.expectReady().Times(
      0); // parent_init_target_ ready will be called by onConfigUpdateFailed
  context_init_manager_.initialize(init_watcher_);

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const auto decoded_resources = TestUtility::decodeResources({resource, resource});
  EXPECT_THROW_WITH_MESSAGE(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"),
                            EnvoyException,
                            "Error adding/updating scoped route(s): duplicate scoped route "
                            "configuration 'foo_scope' found");
}

// Tests duplicate resources in the same update, should be fully rejected.
TEST_F(ScopedRdsTest, InvalidDuplicateResourceDelta) {
  setup();
  init_watcher_.expectReady().Times(0);
  context_init_manager_.initialize(init_watcher_);

  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const auto decoded_resources = TestUtility::decodeResources({resource, resource});
  EXPECT_THROW_WITH_MESSAGE(
      srds_subscription_->onConfigUpdate(decoded_resources.refvec_, {}, "1"), EnvoyException,
      "Error adding/updating scoped route(s): duplicate scoped route configuration 'foo_scope' "
      "found");
  EXPECT_EQ(
      // Fully rejected.
      0UL, server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
               .value());
  // Scope key "x-foo-key" points to nowhere.
  ASSERT_THAT(getScopedRdsProvider(), Not(IsNull()));
  ASSERT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>(), Not(IsNull()));
  EXPECT_THAT(getScopedRdsProvider()->config<ScopedConfigImpl>()->getRouteConfig(
                  TestRequestHeaderMapImpl{{"Addr", "x-foo-key;x-foo-key"}}),
              IsNull());
  EXPECT_EQ(server_factory_context_.scope_.counter("foo.rds.foo_routes.config_reload").value(),
            0UL);
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
  init_watcher_.expectReady();
  context_init_manager_.initialize(init_watcher_);
  auto message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::ScopedRoutesConfigDump&>(
          *message_ptr);

  // No routes at all(no SRDS push yet), no last_updated timestamp
  envoy::admin::v3::ScopedRoutesConfigDump expected_config_dump;
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
      server_factory_context_, context_init_manager_, "foo.", *config_provider_manager_);
  message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump2 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::ScopedRoutesConfigDump&>(
          *message_ptr);
  TestUtility::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
       route_configuration_name: foo-route-config2
       key:
         fragments: { string_key: "172.10.10.20" }
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_scoped_route_configs:
)EOF",
                            expected_config_dump);
  EXPECT_THAT(expected_config_dump, ProtoEq(scoped_routes_config_dump2));

  // Now SRDS kicks off.
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  const auto resource = parseScopedRouteConfigurationFromYaml(R"EOF(
name: dynamic-foo
route_configuration_name: dynamic-foo-route-config
key:
  fragments: { string_key: "172.30.30.10" }
)EOF");

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891567));
  const auto decoded_resources = TestUtility::decodeResources({resource});
  srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1");

  TestUtility::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
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
        "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
        route_configuration_name: dynamic-foo-route-config
        key:
          fragments: { string_key: "172.30.30.10" }
    last_updated:
      seconds: 1234567891
      nanos: 567000000
    version_info: "1"
)EOF",
                            expected_config_dump);
  message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump3 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_THAT(expected_config_dump, ProtoEq(scoped_routes_config_dump3));

  srds_subscription_->onConfigUpdate({}, "2");
  TestUtility::loadFromYaml(R"EOF(
inline_scoped_route_configs:
  - name: foo-scoped-routes
    scoped_route_configs:
     - name: foo
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
       route_configuration_name: foo-route-config
       key:
         fragments: { string_key: "172.10.10.10" }
     - name: foo2
       "@type": type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration
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
  message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["route_scopes"]();
  const auto& scoped_routes_config_dump4 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::ScopedRoutesConfigDump&>(
          *message_ptr);
  EXPECT_THAT(expected_config_dump, ProtoEq(scoped_routes_config_dump4));
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
                   server_factory_context_,
                   Envoy::Config::ConfigProviderManager::NullOptionalArg()),
               ".*");
}

// Tests whether scope key conflict with updated scopes is ignored.
TEST_F(ScopedRdsTest, IgnoreConflictWithUpdatedScopeDelta) {
  setup();
  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: bar_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);

  // Delta API.
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  context_init_manager_.initialize(init_watcher_);
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, {}, "1"));
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);

  const std::string config_yaml3 = R"EOF(
name: bar_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource_3 = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml4 = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_4 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  const auto decoded_resources_2 = TestUtility::decodeResources({resource_3, resource_4});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_2.refvec_, {}, "2"));
  EXPECT_EQ(2UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);
}

// Tests whether scope key conflict with updated scopes is ignored.
TEST_F(ScopedRdsTest, IgnoreConflictWithUpdatedScopeSotW) {
  setup();
  const std::string config_yaml = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml2 = R"EOF(
name: bar_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_2 = parseScopedRouteConfigurationFromYaml(config_yaml2);

  // Delta API.
  const auto decoded_resources = TestUtility::decodeResources({resource, resource_2});
  context_init_manager_.initialize(init_watcher_);
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources.refvec_, "1"));
  EXPECT_EQ(1UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);

  const std::string config_yaml3 = R"EOF(
name: bar_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-foo-key
)EOF";
  const auto resource_3 = parseScopedRouteConfigurationFromYaml(config_yaml);
  const std::string config_yaml4 = R"EOF(
name: foo_scope
route_configuration_name: foo_routes
key:
  fragments:
    - string_key: x-bar-key
)EOF";
  const auto resource_4 = parseScopedRouteConfigurationFromYaml(config_yaml2);
  const auto decoded_resources_2 = TestUtility::decodeResources({resource_3, resource_4});
  EXPECT_NO_THROW(srds_subscription_->onConfigUpdate(decoded_resources_2.refvec_, "2"));
  EXPECT_EQ(2UL,
            server_factory_context_.scope_.counter("foo.scoped_rds.foo_scoped_routes.config_reload")
                .value());
  EXPECT_EQ(getScopedRouteMap().size(), 2);
}

} // namespace
} // namespace Router
} // namespace Envoy
