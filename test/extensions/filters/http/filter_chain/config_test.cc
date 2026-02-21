#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.validate.h"

#include "source/extensions/filters/http/filter_chain/config.h"
#include "source/extensions/filters/http/filter_chain/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {
namespace {

TEST(FilterChainFilterFactoryTest, FilterChainFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.header_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        mutations:
          request_mutations:
          - remove: "test"
  )EOF";

  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterRecursiveConfig) {
  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.filter_chain
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
        filter_chain:
          filters:
          - name: envoy.filters.http.header_mutation
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
              mutations:
                request_mutations:
                - remove: "test"
  )EOF";

  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      { auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context); },
      EnvoyException, "FilterChain filter cannot be configured recursively.");
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterEmptyConfig) {
  envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterChainFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_)).Times(0);
  cb(filter_callback);
}

TEST(FilterChainFilterFactoryTest, FilterChainFilterRouteSpecificConfigWithFilterChain) {
  FilterChainFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.header_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        mutations:
          request_mutations:
          - remove: "test"
  )EOF";

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_string, *proto_config);

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, factory_context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  EXPECT_TRUE(route_config.get());

  const auto* inflated = dynamic_cast<const FilterChainPerRouteConfig*>(route_config.get());
  EXPECT_TRUE(inflated);
  EXPECT_TRUE(inflated->filterChain().has_value());
}

TEST(FilterChainFilterFactoryTest,
     FilterChainFilterRouteSpecificConfigWithFilterChainRecursiveConfig) {
  FilterChainFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: envoy.filters.http.filter_chain
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute
        filter_chain:
          filters:
          - name: envoy.filters.http.header_mutation
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
              mutations:
                request_mutations:
                - remove: "test"
  )EOF";

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_string, *proto_config);

  EXPECT_THROW_WITH_MESSAGE(
      {
        auto route_config = factory.createRouteSpecificFilterConfig(
            *proto_config, factory_context, ProtobufMessage::getNullValidationVisitor());
      },
      EnvoyException, "FilterChain filter cannot be configured recursively.");
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
