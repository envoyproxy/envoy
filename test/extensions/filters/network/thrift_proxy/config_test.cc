#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"

#include "extensions/filters/network/thrift_proxy/config.h"
#include "extensions/filters/network/thrift_proxy/filters/factory_base.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

std::vector<envoy::extensions::filters::network::thrift_proxy::v3::TransportType>
getTransportTypes() {
  std::vector<envoy::extensions::filters::network::thrift_proxy::v3::TransportType> v;
  int transport = envoy::extensions::filters::network::thrift_proxy::v3::TransportType_MIN;
  while (transport <= envoy::extensions::filters::network::thrift_proxy::v3::TransportType_MAX) {
    v.push_back(static_cast<envoy::extensions::filters::network::thrift_proxy::v3::TransportType>(
        transport));
    transport++;
  }
  return v;
}

std::vector<envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType>
getProtocolTypes() {
  std::vector<envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType> v;
  int protocol = envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType_MIN;
  while (protocol <= envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType_MAX) {
    v.push_back(
        static_cast<envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType>(protocol));
    protocol++;
  }
  return v;
}

envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy
parseThriftProxyFromV2Yaml(const std::string& yaml) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy thrift_proxy;
  TestUtility::loadFromYaml(yaml, thrift_proxy);
  return thrift_proxy;
}

} // namespace

class ThriftFilterConfigTestBase {
public:
  void testConfig(envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config) {
    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_); });
    EXPECT_TRUE(factory_.isTerminalFilter());

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ThriftProxyFilterConfigFactory factory_;
};

class ThriftFilterConfigTest : public testing::Test, public ThriftFilterConfigTestBase {};

class ThriftFilterTransportConfigTest
    : public testing::TestWithParam<
          envoy::extensions::filters::network::thrift_proxy::v3::TransportType>,
      public ThriftFilterConfigTestBase {};

INSTANTIATE_TEST_SUITE_P(TransportTypes, ThriftFilterTransportConfigTest,
                         testing::ValuesIn(getTransportTypes()));

class ThriftFilterProtocolConfigTest
    : public testing::TestWithParam<
          envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType>,
      public ThriftFilterConfigTestBase {};

INSTANTIATE_TEST_SUITE_P(ProtocolTypes, ThriftFilterProtocolConfigTest,
                         testing::ValuesIn(getProtocolTypes()));

TEST_F(ThriftFilterConfigTest, ValidateFail) {
  EXPECT_THROW(factory_.createFilterFactoryFromProto(
                   envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy(), context_),
               ProtoValidationException);
}

TEST_F(ThriftFilterConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

TEST_P(ThriftFilterTransportConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.set_transport(GetParam());
  testConfig(config);
}

TEST_P(ThriftFilterProtocolConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.set_protocol(GetParam());
  testConfig(config);
}

TEST_F(ThriftFilterConfigTest, ThriftProxyWithEmptyProto) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      *dynamic_cast<envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy*>(
          factory_.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

// Test config with an explicitly defined router filter.
TEST_F(ThriftFilterConfigTest, ThriftProxyWithExplicitRouterConfig) {
  const std::string yaml = R"EOF(
stat_prefix: thrift
route_config:
  name: local_route
thrift_filters:
  - name: envoy.filters.thrift.router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV2Yaml(yaml);
  testConfig(config);
}

// Test config with an unknown filter.
TEST_F(ThriftFilterConfigTest, ThriftProxyWithUnknownFilter) {
  const std::string yaml = R"EOF(
stat_prefix: thrift
route_config:
  name: local_route
thrift_filters:
  - name: no_such_filter
  - name: envoy.filters.thrift.router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV2Yaml(yaml);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "no_such_filter");
}

// Test config with multiple filters.
TEST_F(ThriftFilterConfigTest, ThriftProxyWithMultipleFilters) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
route_config:
  name: local_route
thrift_filters:
  - name: envoy.filters.thrift.mock_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  - name: envoy.filters.thrift.router
)EOF";

  ThriftFilters::MockFilterConfigFactory factory;
  Registry::InjectFactory<ThriftFilters::NamedThriftFilterConfigFactory> registry(factory);

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV2Yaml(yaml);
  testConfig(config);

  EXPECT_EQ(1, factory.config_struct_.fields_size());
  EXPECT_EQ("value", factory.config_struct_.fields().at("key").string_value());
  EXPECT_EQ("thrift.ingress.", factory.config_stat_prefix_);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
