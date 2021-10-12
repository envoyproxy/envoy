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
#include "source/common/router/rds/rds_route_config_provider_impl.h"
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
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

using ::Envoy::Matchers::MockStringMatcher;
using ::Envoy::Matchers::UniversalStringMatcher;

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

using RdsRouteConfigProviderImpl = Envoy::Router::Rds::RdsRouteConfigProviderImpl<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>;

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

  envoy::extensions::filters::network::thrift_proxy::v3::Trds rds_;
  RouteConfigProviderManagerImplPtr route_config_provider_manager_;
  RouteConfigProviderSharedPtr provider_;
};

envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration
parseRouteConfigurationFromV3Yaml(const std::string& yaml) {
  envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
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

  envoy::extensions::filters::network::thrift_proxy::v3::Trds rds2;
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

} // namespace
} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
