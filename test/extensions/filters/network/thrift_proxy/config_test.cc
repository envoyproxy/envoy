#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"
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
  // Note: ProtocolType_MAX is TTwitter, which is deprecated.
  while (protocol < envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType_MAX) {
    v.push_back(
        static_cast<envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType>(protocol));
    protocol++;
  }
  return v;
}

envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy
parseThriftProxyFromV3Yaml(const std::string& yaml) {
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
    EXPECT_TRUE(factory_.isTerminalFilterByProto(config, context_.getServerFactoryContext()));

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  Event::SimulatedTimeSystem time_system_;
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

// Test config with an invalid cluster_header.
TEST_F(ThriftFilterConfigTest, RouterConfigWithInvalidClusterHeader) {
  const std::string yaml = R"EOF(
stat_prefix: thrift
route_config:
  name: local_route
  routes:
    match:
      method_name: A
    route:
      cluster_header: A
thrift_filters:
  - name: envoy.filters.thrift.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
  std::string header = "A";
  header.push_back('\000'); // Add an invalid character for http header.
  config.mutable_route_config()->mutable_routes()->at(0).mutable_route()->set_cluster_header(
      header);
  EXPECT_THROW(factory_.createFilterFactoryFromProto(config, context_), ProtoValidationException);
}

// Test config with an explicitly defined router filter.
TEST_F(ThriftFilterConfigTest, ThriftProxyWithExplicitRouterConfig) {
  const std::string yaml = R"EOF(
stat_prefix: thrift
route_config:
  name: local_route
thrift_filters:
  - name: envoy.filters.thrift.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
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
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);

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
  - name: envoy.filters.thrift.mock_decoder_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  - name: envoy.filters.thrift.mock_encoder_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  - name: envoy.filters.thrift.mock_bidirectional_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  - name: envoy.filters.thrift.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router
)EOF";

  ThriftFilters::MockBidirectionalFilterConfigFactory factory;
  Registry::InjectFactory<ThriftFilters::NamedThriftFilterConfigFactory> registry(factory);

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
  testConfig(config);

  EXPECT_EQ(1, factory.config_struct_.fields_size());
  EXPECT_EQ("value", factory.config_struct_.fields().at("key").string_value());
  EXPECT_EQ("thrift.ingress.", factory.config_stat_prefix_);
}

// Test config with payload passthrough enabled.
TEST_F(ThriftFilterConfigTest, ThriftProxyPayloadPassthrough) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
payload_passthrough: true
route_config:
  name: local_route
thrift_filters:
  - name: envoy.filters.thrift.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.router.v3.Router
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
  testConfig(config);

  EXPECT_EQ(true, config.payload_passthrough());
}

TEST_F(ThriftFilterConfigTest, ThriftProxyTrds) {
  const std::string config_yaml = R"EOF(
stat_prefix: ingress
trds:
  config_source: { resource_api_version: V3, ads: {} }
  route_config_name: test_route
)EOF";

  const std::string response_yaml = (R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
    name: test_route
    routes: null
)EOF");

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(config_yaml);
  Matchers::UniversalStringMatcher universal_name_matcher;
  Network::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config, context_);
  auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(response);
  context_.server_factory_context_.cluster_manager_.subscription_factory_.callbacks_
      ->onConfigUpdate(decoded_resources.refvec_, response.version_info());
  auto message_ptr = context_.admin_.config_tracker_.config_tracker_callbacks_["trds_routes"](
      universal_name_matcher);
  const auto& dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump.dynamic_route_configs().size());
  EXPECT_EQ(0, dump.static_route_configs().size());
}

TEST_F(ThriftFilterConfigTest, ThriftProxyBothTrdsAndRouteConfig) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
route_config:
  name: local_route
trds:
  config_source: { resource_api_version: V3, ads: {} }
  route_config_name: test_route
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "both trds and route_config is present in ThriftProxy");
}

TEST_F(ThriftFilterConfigTest, ThriftProxyTrdsApiConfigSource) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
trds:
  config_source:
    resource_api_version: V3
    api_config_source: { api_type: GRPC, transport_api_version: V3 }
  route_config_name: test_route
)EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config =
      parseThriftProxyFromV3Yaml(yaml);
  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_), EnvoyException,
                          "trds supports only aggregated api_type in api_config_source");
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
