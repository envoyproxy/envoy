#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"

#include "extensions/filters/network/dubbo_proxy/config.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter_config.h"

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

DubboProxyProto parseDubboProxyFromV2Yaml(const std::string& yaml) {
  DubboProxyProto dubbo_proxy;
  TestUtility::loadFromYaml(yaml, dubbo_proxy);
  return dubbo_proxy;
}

} // namespace

class DubboFilterConfigTestBase {
public:
  void testConfig(DubboProxyProto& config) {
    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  DubboProxyFilterConfigFactory factory_;
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
  EXPECT_TRUE(factory.isTerminalFilter());
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
    route_config:
      name: local_route
    dubbo_filters:
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboProxyProto config = parseDubboProxyFromV2Yaml(yaml);
  testConfig(config);
}

// Test config with an unknown filter.
TEST_F(DubboFilterConfigTest, DubboProxyWithUnknownFilter) {
  const std::string yaml = R"EOF(
    stat_prefix: dubbo
    route_config:
      name: local_route
    dubbo_filters:
      - name: no_such_filter
      - name: envoy.filters.dubbo.router
    )EOF";

  DubboProxyProto config = parseDubboProxyFromV2Yaml(yaml);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "no_such_filter");
}

// Test config with multiple filters.
TEST_F(DubboFilterConfigTest, DubboProxyWithMultipleFilters) {
  const std::string yaml = R"EOF(
    stat_prefix: ingress
    route_config:
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

  DubboProxyProto config = parseDubboProxyFromV2Yaml(yaml);
  testConfig(config);

  EXPECT_EQ(1, factory.config_struct_.fields_size());
  EXPECT_EQ("test_service", factory.config_struct_.fields().at("name").string_value());
  EXPECT_EQ("dubbo.ingress.", factory.config_stat_prefix_);
}

TEST_F(DubboFilterConfigTest, CreateFilterChain) {
  const std::string yaml = R"EOF(
    stat_prefix: ingress
    route_config:
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

  DubboProxyProto dubbo_config = parseDubboProxyFromV2Yaml(yaml);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  DubboFilters::MockFilterChainFactoryCallbacks callbacks;
  ConfigImpl config(dubbo_config, context);
  EXPECT_CALL(callbacks, addDecoderFilter(_)).Times(2);
  config.createFilterChain(callbacks);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
