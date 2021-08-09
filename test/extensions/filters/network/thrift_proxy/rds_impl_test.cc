#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/config_dump.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/json/json_loader.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

using ::Envoy::Matchers::MockStringMatcher;
using ::Envoy::Matchers::UniversalStringMatcher;

envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy
parseThriftProxyFromYaml(const std::string& yaml_string) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy thrift_proxy;
  TestUtility::loadFromYaml(yaml_string, thrift_proxy);
  return thrift_proxy;
}

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() {
    // For server_factory_context
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    EXPECT_CALL(validation_context_, dynamicValidationVisitor())
        .WillRepeatedly(ReturnRef(validation_visitor_));

    ON_CALL(outer_init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    ON_CALL(outer_init_manager_, initialize(_))
        .WillByDefault(Invoke(
            [this](const Init::Watcher& watcher) { init_target_handle_->initialize(watcher); }));
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Init::MockManager> outer_init_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Envoy::Config::SubscriptionCallbacks* rds_callbacks_{};
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

class RdsImplTest : public RdsTestBase {
public:
  RdsImplTest() {
    EXPECT_CALL(server_factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_ =
        std::make_unique<RouteConfigProviderManagerImpl>(server_factory_context_.admin_);
  }
  ~RdsImplTest() override { server_factory_context_.thread_local_.shutdownThread(); }

  void setup() {
    std::string config_yaml = R"EOF(
rds:
  config_source:
    api_config_source:
      api_type: REST
      cluster_names:
      - foo_cluster
      refresh_delay: 1s
  route_config_name: foo_route_config
    )EOF";

    EXPECT_CALL(outer_init_manager_, add(_));
    rds_ = route_config_provider_manager_->createRdsRouteConfigProvider(
        parseThriftProxyFromYaml(config_yaml).rds(), server_factory_context_, "foo.",
        outer_init_manager_);
    rds_callbacks_ = server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*server_factory_context_.cluster_manager_.subscription_factory_.subscription_,
                start(_));
    outer_init_manager_.initialize(init_watcher_);
  }

  RouteConstSharedPtr route(const MessageMetadata& metadata) {
    return rds_->config()->route(metadata, 12345);
  }

  RouteConfigProviderManagerImplPtr route_config_provider_manager_;
  RouteConfigProviderSharedPtr rds_;
};

TEST_F(RdsImplTest, DestroyDuringInitialize) {
  InSequence s;
  setup();
  EXPECT_CALL(init_watcher_, ready());
  rds_.reset();
}

TEST_F(RdsImplTest, Basic) {
  InSequence s;
  Buffer::OwnedImpl empty;
  Buffer::OwnedImpl data;

  setup();

  // Make sure the initial empty route table works.
  MessageMetadata md;
  md.setMethodName("foo");
  EXPECT_EQ(nullptr, route(md));

  // Initial request.
  const std::string response1_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration",
      "name": "foo_route_config",
      "routes": null
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response1);

  EXPECT_CALL(init_watcher_, ready());
  rds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info());
  md.setMethodName("bar");
  EXPECT_EQ(nullptr, route(md));

  // 2nd request with same response. Based on hash should not reload config.
  rds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info());
  EXPECT_EQ(nullptr, route(md));

  // Load the config and verified shared count.
  // ConfigConstSharedPtr is shared between: RouteConfigUpdateReceiverImpl, rds_ (via tls_), and
  // config local var below.
  ConfigConstSharedPtr config = rds_->config();
  EXPECT_EQ(3, config.use_count());

  // Third request.
  const std::string response2_json = R"EOF(
{
  "version_info": "2",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration",
      "name": "foo_route_config",
      "routes": [
        {
          "match":
          {
            "method_name": "bar"
          },
          "route":
          {
            "cluster": "foo"
          }
        }
      ]
    }
  ]
}
  )EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_json);
  const auto decoded_resources_2 = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response2);

  // Make sure we don't lookup/verify clusters.
  EXPECT_CALL(server_factory_context_.cluster_manager_, getThreadLocalCluster(Eq("bar"))).Times(0);
  rds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info());
  EXPECT_EQ("foo", route(md)->routeEntry()->clusterName());

  // Old config use count should be 1 now.
  EXPECT_EQ(1, config.use_count());
  EXPECT_EQ(2UL, scope_.counter("foo.rds.foo_route_config.config_reload").value());
  EXPECT_TRUE(scope_.findGaugeByString("foo.rds.foo_route_config.config_reload_time_ms"));
}

// Validate behavior when the config is delivered but it fails PGV validation.
TEST_F(RdsImplTest, FailureInvalidConfig) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration",
      "name": "INVALID_NAME_FOR_route_config",
      "routes": null
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response1);

  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      rds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()),
      EnvoyException,
      "Unexpected RDS configuration (expecting foo_route_config): INVALID_NAME_FOR_route_config");
}

// Validate behavior when the config fails delivery at the subscription level.
TEST_F(RdsImplTest, FailureSubscription) {
  InSequence s;

  setup();

  EXPECT_CALL(init_watcher_, ready());
  // onConfigUpdateFailed() should not be called for gRPC stream connection failure
  rds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, {});
}

class RouteConfigProviderManagerImplTest : public RdsTestBase {
public:
  void setup() {
    // Get a RouteConfigProvider. This one should create an entry in the RouteConfigProviderManager.
    rds_.set_route_config_name("foo_route_config");
    rds_.mutable_config_source()->set_path("foo_path");
    provider_ = route_config_provider_manager_->createRdsRouteConfigProvider(
        rds_, server_factory_context_, "foo_prefix.", outer_init_manager_);
    rds_callbacks_ = server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  }

  RouteConfigProviderManagerImplTest() {
    EXPECT_CALL(server_factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_ =
        std::make_unique<RouteConfigProviderManagerImpl>(server_factory_context_.admin_);
  }

  ~RouteConfigProviderManagerImplTest() override {
    server_factory_context_.thread_local_.shutdownThread();
  }

  envoy::extensions::filters::network::thrift_proxy::v3::Rds rds_;
  RouteConfigProviderManagerImplPtr route_config_provider_manager_;
  RouteConfigProviderSharedPtr provider_;
};

envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration
parseRouteConfigurationFromV3Yaml(const std::string& yaml, bool avoid_boosting = true) {
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config, true, avoid_boosting);
  return route_config;
}

TEST_F(RouteConfigProviderManagerImplTest, ConfigDump) {
  UniversalStringMatcher universal_name_matcher;
  auto message_ptr =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"](
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

  // Only static route.
  server_factory_context_.cluster_manager_.initializeClusters({"baz"}, {});
  RouteConfigProviderPtr static_config =
      route_config_provider_manager_->createStaticRouteConfigProvider(
          parseRouteConfigurationFromV3Yaml(config_yaml), server_factory_context_);
  message_ptr = server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"](
      universal_name_matcher);
  const auto& route_config_dump2 =
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
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump2.DebugString());

  // Static + dynamic.
  setup();
  EXPECT_CALL(*server_factory_context_.cluster_manager_.subscription_factory_.subscription_,
              start(_));
  outer_init_manager_.initialize(init_watcher_);

  const std::string response1_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration",
      "name": "foo_route_config",
      "routes": null
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response1);

  EXPECT_CALL(init_watcher_, ready());
  rds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info());
  message_ptr = server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"](
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
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump3.DebugString());

  MockStringMatcher mock_name_matcher;
  EXPECT_CALL(mock_name_matcher, match("foo")).WillOnce(Return(true));
  EXPECT_CALL(mock_name_matcher, match("foo_route_config")).WillOnce(Return(false));
  message_ptr = server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"](
      mock_name_matcher);
  const auto& route_config_dump4 =
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
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump4.DebugString());

  EXPECT_CALL(mock_name_matcher, match("foo")).WillOnce(Return(false));
  EXPECT_CALL(mock_name_matcher, match("foo_route_config")).WillOnce(Return(true));
  message_ptr = server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"](
      mock_name_matcher);
  const auto& route_config_dump5 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  TestUtility::loadFromYaml(R"EOF(
dynamic_route_configs:
  - version_info: "1"
    route_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
      name: foo_route_config
      routes:
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump5.DebugString());
}

TEST_F(RouteConfigProviderManagerImplTest, Basic) {
  Buffer::OwnedImpl data;

  // Get a RouteConfigProvider. This one should create an entry in the RouteConfigProviderManager.
  setup();

  EXPECT_FALSE(provider_->configInfo().has_value());

  const auto route_config = parseRouteConfigurationFromV3Yaml(R"EOF(
name: foo_route_config
routes:
  - match:
      method_name: bar
    route:
      cluster: baz
)EOF");
  const auto decoded_resources = TestUtility::decodeResources({route_config});

  server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "1");

  RouteConfigProviderSharedPtr provider2 =
      route_config_provider_manager_->createRdsRouteConfigProvider(
          rds_, server_factory_context_, "foo_prefix", outer_init_manager_);

  // provider2 should have route config immediately after create
  EXPECT_TRUE(provider2->configInfo().has_value());

  EXPECT_EQ(provider_, provider2) << "fail to obtain the same rds config provider object";

  // So this means that both provider have same subscription.
  EXPECT_EQ(&dynamic_cast<RdsRouteConfigProviderImpl&>(*provider_).subscription(),
            &dynamic_cast<RdsRouteConfigProviderImpl&>(*provider2).subscription());
  EXPECT_EQ(&provider_->configInfo().value().config_, &provider2->configInfo().value().config_);

  envoy::extensions::filters::network::thrift_proxy::v3::Rds rds2;
  rds2.set_route_config_name("foo_route_config");
  rds2.mutable_config_source()->set_path("bar_path");
  RouteConfigProviderSharedPtr provider3 =
      route_config_provider_manager_->createRdsRouteConfigProvider(
          rds2, server_factory_context_, "foo_prefix", outer_init_manager_);
  EXPECT_NE(provider3, provider_);
  server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, "provider3");
  UniversalStringMatcher universal_name_matcher;
  EXPECT_EQ(2UL, route_config_provider_manager_->dumpRouteConfigs(universal_name_matcher)
                     ->dynamic_route_configs()
                     .size());

  provider_.reset();
  provider2.reset();

  // All shared_ptrs to the provider pointed at by provider1, and provider2 have been deleted, so
  // now we should only have the provider pointed at by provider3.
  auto dynamic_route_configs =
      route_config_provider_manager_->dumpRouteConfigs(universal_name_matcher)
          ->dynamic_route_configs();
  EXPECT_EQ(1UL, dynamic_route_configs.size());

  // Make sure the left one is provider3
  EXPECT_EQ("provider3", dynamic_route_configs[0].version_info());

  provider3.reset();

  EXPECT_EQ(0UL, route_config_provider_manager_->dumpRouteConfigs(universal_name_matcher)
                     ->dynamic_route_configs()
                     .size());
}

TEST_F(RouteConfigProviderManagerImplTest, SameProviderOnTwoInitManager) {
  Buffer::OwnedImpl data;
  // Get a RouteConfigProvider. This one should create an entry in the RouteConfigProviderManager.
  setup();

  EXPECT_FALSE(provider_->configInfo().has_value());

  NiceMock<Server::Configuration::MockServerFactoryContext> mock_factory_context2;

  Init::WatcherImpl real_watcher("real", []() {});
  Init::ManagerImpl real_init_manager("real");

  RouteConfigProviderSharedPtr provider2 =
      route_config_provider_manager_->createRdsRouteConfigProvider(rds_, mock_factory_context2,
                                                                   "foo_prefix", real_init_manager);

  EXPECT_FALSE(provider2->configInfo().has_value());

  EXPECT_EQ(provider_, provider2) << "fail to obtain the same rds config provider object";
  real_init_manager.initialize(real_watcher);
  EXPECT_EQ(Init::Manager::State::Initializing, real_init_manager.state());

  {
    const auto route_config = parseRouteConfigurationFromV3Yaml(R"EOF(
name: foo_route_config
routes:
  - match:
      method_name: bar
    route:
      cluster: baz
)EOF");
    const auto decoded_resources = TestUtility::decodeResources({route_config});

    server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
        decoded_resources.refvec_, "1");

    EXPECT_TRUE(provider_->configInfo().has_value());
    EXPECT_TRUE(provider2->configInfo().has_value());
    EXPECT_EQ(Init::Manager::State::Initialized, real_init_manager.state());
  }
}

TEST_F(RouteConfigProviderManagerImplTest, OnConfigUpdateEmpty) {
  setup();
  EXPECT_CALL(*server_factory_context_.cluster_manager_.subscription_factory_.subscription_,
              start(_));
  outer_init_manager_.initialize(init_watcher_);
  EXPECT_CALL(init_watcher_, ready());
  server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate({}, "");
}

TEST_F(RouteConfigProviderManagerImplTest, OnConfigUpdateWrongSize) {
  setup();
  EXPECT_CALL(*server_factory_context_.cluster_manager_.subscription_factory_.subscription_,
              start(_));
  outer_init_manager_.initialize(init_watcher_);
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration route_config;
  const auto decoded_resources = TestUtility::decodeResources({route_config, route_config});
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
          decoded_resources.refvec_, ""),
      EnvoyException, "Unexpected RDS resource length: 2");
}

} // namespace
} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
