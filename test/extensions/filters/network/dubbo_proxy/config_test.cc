#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.validate.h"

#include "source/extensions/filters/network/dubbo_proxy/config.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter_config.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using DubboProxyProto = envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy;

namespace {

DubboProxyProto parseDubboProxyFromV3Yaml(const std::string& yaml) {
  DubboProxyProto dubbo_proxy;
  TestUtility::loadFromYaml(yaml, dubbo_proxy);
  return dubbo_proxy;
}

} // namespace

class DubboFilterConfigTestBase {
public:
  void testConfig(DubboProxyProto& config) {
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_))
        .WillOnce(testing::Invoke(
            [this](Network::ReadFilterSharedPtr filter) { filter_ = std::move(filter); }));

    cb(connection);

    ASSERT(filter_ != nullptr);
    typed_filter_ = dynamic_cast<ConnectionManager*>(filter_.get());
  }

  Network::FilterFactoryCb cb;

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  DubboProxyFilterConfigFactory factory_;
  Network::ReadFilterSharedPtr filter_;
  ConnectionManager* typed_filter_{};
};

class DubboFilterConfigTest : public DubboFilterConfigTestBase, public testing::Test {};

TEST_F(DubboFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(DubboProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy(), context),
               ProtoValidationException);
}

TEST_F(DubboFilterConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config{};

  config.set_stat_prefix("my_stat_prefix");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  DubboProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  EXPECT_TRUE(factory.isTerminalFilterByProto(config, context.serverFactoryContext()));
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_F(DubboFilterConfigTest, DubboProxyWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  DubboProxyFilterConfigFactory factory;
  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      *dynamic_cast<envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Test config with an explicitly defined router filter.
TEST_F(DubboFilterConfigTest, DubboProxyWithExplicitRouterConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: dubbo
    multiple_route_config:
      name: local_route
    dubbo_filters:
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboProxyProto config = parseDubboProxyFromV3Yaml(yaml);
  testConfig(config);
}

// Test config with an unknown filter.
TEST_F(DubboFilterConfigTest, DubboProxyWithUnknownFilter) {
  const std::string yaml = R"EOF(
    stat_prefix: dubbo
    multiple_route_config:
      name: local_route
    dubbo_filters:
      - name: no_such_filter
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboProxyProto config = parseDubboProxyFromV3Yaml(yaml);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "no_such_filter");
}

// Test config with multiple filters.
TEST_F(DubboFilterConfigTest, DubboProxyWithMultipleFilters) {
  const std::string yaml = R"EOF(
    stat_prefix: ingress
    multiple_route_config:
      name: local_route
    dubbo_filters:
      - name: envoy.filters.dubbo.mock_filter
        config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: test_service
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboFilters::MockFilterConfigFactory factory;
  Registry::InjectFactory<DubboFilters::NamedDubboFilterConfigFactory> registry(factory);

  DubboProxyProto config = parseDubboProxyFromV3Yaml(yaml);
  testConfig(config);

  EXPECT_EQ(1, factory.config_struct_.fields_size());
  EXPECT_EQ("test_service", factory.config_struct_.fields().at("name").string_value());
  EXPECT_EQ("dubbo.ingress.", factory.config_stat_prefix_);
}

TEST_F(DubboFilterConfigTest, CreateFilterChain) {
  const std::string yaml = R"EOF(
    stat_prefix: ingress
    multiple_route_config:
      name: local_route
    dubbo_filters:
      - name: envoy.filters.dubbo.mock_filter
        config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: test_service
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboFilters::MockFilterConfigFactory factory;
  Registry::InjectFactory<DubboFilters::NamedDubboFilterConfigFactory> registry(factory);

  DubboProxyProto config = parseDubboProxyFromV3Yaml(yaml);
  testConfig(config);

  DubboFilters::MockFilterChainFactoryCallbacks callbacks;
  EXPECT_CALL(callbacks, addDecoderFilter(_));
  EXPECT_CALL(callbacks, addFilter(_));
  typed_filter_->config().filterFactory().createFilterChain(callbacks);
}

TEST_F(DubboFilterConfigTest, DubboProxyDrds) {
  const std::string config_yaml = R"EOF(
stat_prefix: ingress
drds:
  config_source: { resource_api_version: V3, ads: {} }
  route_config_name: test_route
)EOF";

  const std::string response_yaml = (R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.dubbo_proxy.v3.MultipleRouteConfiguration
    name: test_route
    route_config: {}
)EOF");

  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      parseDubboProxyFromV3Yaml(config_yaml);
  Matchers::UniversalStringMatcher universal_name_matcher;
  Network::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config, context_);
  auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration>(response);
  EXPECT_TRUE(context_.server_factory_context_.cluster_manager_.subscription_factory_.callbacks_
                  ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                  .ok());
  auto message_ptr = context_.server_factory_context_.admin_.config_tracker_
                         .config_tracker_callbacks_["drds_routes"](universal_name_matcher);
  const auto& dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump.dynamic_route_configs().size());
  EXPECT_EQ(0, dump.static_route_configs().size());
}

#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
TEST_F(DubboFilterConfigTest, DubboProxyBothDrdsAndRouteConfig) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
route_config:
- name: local_route
drds:
  config_source: { resource_api_version: V3, ads: {} }
  route_config_name: test_route
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      parseDubboProxyFromV3Yaml(yaml);
  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "both drds and route_config is present in DubboProxy");
}

TEST_F(DubboFilterConfigTest, DubboProxyBothMultipleRouteConfigAndRouteConfig) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
route_config:
- name: local_route
multiple_route_config:
  name: local_route_2
  route_config:
  - name: local_route
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      parseDubboProxyFromV3Yaml(yaml);
  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "both mutiple_route_config and route_config is present in DubboProxy");
}
#endif

TEST_F(DubboFilterConfigTest, DubboProxyDrdsApiConfigSource) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
drds:
  config_source:
    resource_api_version: V3
    api_config_source: { api_type: GRPC, transport_api_version: V3 }
  route_config_name: test_route
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      parseDubboProxyFromV3Yaml(yaml);
  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "drds supports only aggregated api_type in api_config_source");
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
