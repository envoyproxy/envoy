#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/rds/common/route_config_provider_manager_impl.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_provider_manager.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Rds {
namespace {

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() {
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(*scope_.rootScope()));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
  }

  ~RdsTestBase() override { server_factory_context_.thread_local_.shutdownThread(); }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

class TestConfig : public Config {
public:
  TestConfig() = default;
  TestConfig(const envoy::config::route::v3::RouteConfiguration& rc,
             Server::Configuration::ServerFactoryContext&, bool)
      : rc_(rc) {}
  const std::string* route(const std::string& name) const {
    for (const auto& virtual_host_config : rc_.virtual_hosts()) {
      if (virtual_host_config.name() == name) {
        return &virtual_host_config.name();
      }
    }
    return nullptr;
  }

private:
  envoy::config::route::v3::RouteConfiguration rc_;
};

class RdsConfigUpdateReceiverTest : public RdsTestBase {
public:
  void setup() {
    config_update_ = std::make_unique<RouteConfigUpdateReceiverImpl>(config_traits_, proto_traits_,
                                                                     server_factory_context_);
  }

  const std::string* route(const std::string& path) {
    return std::static_pointer_cast<const TestConfig>(config_update_->parsedConfiguration())
        ->route(path);
  }

  Common::ProtoTraitsImpl<envoy::config::route::v3::RouteConfiguration, 1> proto_traits_;
  Common::ConfigTraitsImpl<envoy::config::route::v3::RouteConfiguration, TestConfig, TestConfig>
      config_traits_;
  RouteConfigUpdatePtr config_update_;
};

TEST_F(RdsConfigUpdateReceiverTest, OnRdsUpdate) {
  setup();

  EXPECT_TRUE(config_update_->parsedConfiguration());
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_FALSE(config_update_->configInfo().has_value());

  const std::string response1_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": null
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response1_json);

  SystemTime time1(std::chrono::milliseconds(1234567891234));
  timeSystem().setSystemTime(time1);

  EXPECT_TRUE(config_update_->onRdsUpdate(response1, "1"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("1", config_update_->configInfo().value().version_);
  EXPECT_EQ(time1, config_update_->lastUpdated());

  EXPECT_FALSE(config_update_->onRdsUpdate(response1, "2"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_EQ("1", config_update_->configInfo().value().version_);

  std::shared_ptr<const Config> config = config_update_->parsedConfiguration();
  EXPECT_EQ(2, config.use_count());

  const std::string response2_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": [
    {
      "name": "foo",
      "domains": [
        "*"
      ],
    }
  ]
}
  )EOF";

  auto response2 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response2_json);

  SystemTime time2(std::chrono::milliseconds(1234567891235));
  timeSystem().setSystemTime(time2);

  EXPECT_TRUE(config_update_->onRdsUpdate(response2, "2"));
  EXPECT_EQ("foo", *route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("2", config_update_->configInfo().value().version_);
  EXPECT_EQ(time2, config_update_->lastUpdated());

  EXPECT_EQ(1, config.use_count());
}

class RdsConfigProviderManagerTest : public RdsTestBase {
public:
  RdsConfigProviderManagerTest() : manager_(server_factory_context_.admin_) {}

  RouteConfigProviderSharedPtr createDynamic() {
    envoy::extensions::filters::network::thrift_proxy::v3::Trds rds;
    rds.mutable_config_source()->set_path("dummy");
    rds.set_route_config_name("test_route");
    return manager_.createRdsRouteConfigProvider(rds, server_factory_context_, "test_listener.",
                                                 outer_init_manager_);
  }

  RouteConfigProviderSharedPtr createStatic() {
    envoy::config::route::v3::RouteConfiguration route_config;
    TestUtility::loadFromYaml(R"EOF(
name: foo
virtual_hosts: null
)EOF",
                              route_config);
    return manager_.createStaticRouteConfigProvider(route_config, server_factory_context_);
  }

  template <class RouteConfiguration = envoy::config::route::v3::RouteConfiguration>
  void setConfigToDynamicProvider(const std::string& response_json) {
    auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_json);
    const auto decoded_resources = TestUtility::decodeResources<RouteConfiguration>(response);
    THROW_IF_NOT_OK(
        server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
            decoded_resources.refvec_, response.version_info()));
  }

  NiceMock<Init::MockManager> outer_init_manager_;
  Common::RouteConfigProviderManagerImpl<
      envoy::extensions::filters::network::thrift_proxy::v3::Trds,
      envoy::config::route::v3::RouteConfiguration, 1, TestConfig, TestConfig>
      manager_;
};

TEST_F(RdsConfigProviderManagerTest, ProviderAddErase) {
  Matchers::UniversalStringMatcher universal_name_matcher;
  auto config_tracker_callback =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["trds_routes"];

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  auto message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump1 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(0, dump1.dynamic_route_configs().size());
  EXPECT_EQ(0, dump1.static_route_configs().size());

  RouteConfigProviderSharedPtr static_provider = createStatic();
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();
  setConfigToDynamicProvider<>(R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: test_route
    virtual_hosts: null
)EOF");

  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump2 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump2.dynamic_route_configs().size());
  EXPECT_EQ(1, dump2.static_route_configs().size());

  envoy::admin::v3::RoutesConfigDump expected_dump;
  TestUtility::loadFromYaml(R"EOF(
static_route_configs:
  - route_config:
      "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
      name: foo
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_route_configs:
  - version_info: "1"
    route_config:
      "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
      name: test_route
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF",
                            expected_dump);
  EXPECT_EQ(expected_dump.DebugString(), dump2.DebugString());

  EXPECT_EQ(1UL, scope_.counter("test_listener.trds.test_route.config_reload").value());

  static_provider.reset();
  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump3 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump3.dynamic_route_configs().size());
  EXPECT_EQ(0, dump3.static_route_configs().size());

  dynamic_provider.reset();
  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump4 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(0, dump4.dynamic_route_configs().size());
  EXPECT_EQ(0, dump4.static_route_configs().size());
}

TEST_F(RdsConfigProviderManagerTest, FailureInvalidResourceType) {
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();

  const std::string wrong_name = R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: not_test_route
    virtual_hosts: null
)EOF";
  EXPECT_THROW_WITH_MESSAGE(setConfigToDynamicProvider<>(wrong_name), EnvoyException,
                            "Unexpected TRDS configuration (expecting test_route): not_test_route");

  const std::string wrong_type = R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
    name: test_route
    routes: null
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      setConfigToDynamicProvider<
          envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(wrong_type),
      EnvoyException,
      "Unexpected TRDS configuration type (expecting envoy.config.route.v3.RouteConfiguration): "
      "envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration");
}

} // namespace
} // namespace Rds
} // namespace Envoy
