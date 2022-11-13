#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/generic_proxy.pb.validate.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/v3/route.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/config.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

// Keep empty until merge the latest API from main.
TEST(FactoryTest, FactoryTest) {
  const std::string yaml_config = R"EOF(
    stat_prefix: config_test
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    route_config:
      name: test-routes
      routes:
        matcher_tree:
          input:
            name: request-service
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.ServiceMatchInput
          exact_match_map:
            map:
              service_name_0:
                matcher:
                  matcher_list:
                    matchers:
                    - predicate:
                        single_predicate:
                          input:
                            name: request-properties
                            typed_config:
                              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                              property_name: version
                          value_match:
                            exact: v1
                      on_match:
                        action:
                          name: route
                          typed_config:
                            "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
                            cluster: cluster_0
    )EOF";

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;
  ProxyConfig proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  EXPECT_NE(nullptr, factory.createFilterFactoryFromProto(proto_config, factory_context));
}

TEST(FactoryTest, GenericRds) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    generic_rds:
      config_source: { resource_api_version: V3, ads: {} }
      route_config_name: test_route
    )EOF";

  const std::string response_yaml = (R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.v3.RouteConfiguration
    name: test_route
    routes: {}
)EOF");

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  Matchers::UniversalStringMatcher universal_name_matcher;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, factory_context);
  auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources = TestUtility::decodeResources<
      envoy::extensions::filters::network::generic_proxy::v3::RouteConfiguration>(response);
  factory_context.server_factory_context_.cluster_manager_.subscription_factory_.callbacks_
      ->onConfigUpdate(decoded_resources.refvec_, response.version_info());
  auto message_ptr =
      factory_context.admin_.config_tracker_.config_tracker_callbacks_["genericrds_routes"](
          universal_name_matcher);
  const auto& dump =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump.dynamic_route_configs().size());
  EXPECT_EQ(0, dump.static_route_configs().size());
}

TEST(FactoryTest, GenericRdsApiConfigSource) {
  const std::string config_yaml = R"EOF(
    stat_prefix: ingress
    filters:
    - name: envoy.filters.generic.router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
    codec_config:
      name: fake
      typed_config:
        "@type": type.googleapis.com/xds.type.v3.TypedStruct
        type_url: envoy.generic_proxy.codecs.fake.type
        value: {}
    generic_rds:
      config_source:
        resource_api_version: V3
        api_config_source: { api_type: GRPC, transport_api_version: V3 }
      route_config_name: test_route
    )EOF";

  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  Factory factory;

  envoy::extensions::filters::network::generic_proxy::v3::GenericProxy config;
  TestUtility::loadFromYaml(config_yaml, config);

  EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(config, factory_context),
                          EnvoyException,
                          "genericrds supports only aggregated api_type in api_config_source");
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
