#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.validate.h"

#include "source/extensions/filters/http/filter_chain/config.h"
#include "source/extensions/filters/http/filter_chain/filter.h"

#include "test/integration/filters/test_filters.pb.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
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

using StatusHelpers::HasStatus;

TEST(FilterChainFilterFactoryTest, FilterChainFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  default_filter_chain:
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
  default_filter_chain:
    filters:
    - name: envoy.filters.http.filter_chain
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
        default_filter_chain:
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
  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument,
                                   "FilterChain filter cannot be configured recursively."));
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

  const std::string empty_stats_prefix;
  Server::Configuration::ExtraFactoryContext extra_context{
      ProtobufMessage::getNullValidationVisitor(), empty_stats_prefix};
  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createHttpFilterRouteConfig(*proto_config, factory_context, extra_context).value();
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

  const std::string empty_stats_prefix;
  Server::Configuration::ExtraFactoryContext extra_context{
      ProtobufMessage::getNullValidationVisitor(), empty_stats_prefix};
  auto route_config =
      factory.createHttpFilterRouteConfig(*proto_config, factory_context, extra_context);
  EXPECT_THAT(route_config, HasStatus(absl::StatusCode::kInvalidArgument,
                                      "FilterChain filter cannot be configured recursively."));
}

// Records the stats prefix that the filter chain hands to its filters.
class StatsPrefixRecordingFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProto(
      const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    stats_prefix_ = extra_context.stats_prefix;
    return [](Http::FilterChainFactoryCallbacks&) {};
  }
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    return absl::UnimplementedError("unexpected");
  }
  bool isUnifiedFilter() override { return true; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::PassthroughFilterConfig>();
  }
  std::string name() const override { return "stats-prefix-recording-filter"; }

  std::string stats_prefix_;
};

TEST(FilterChainFilterFactoryTest, RouteSpecificConfigUsesStatsPrefixOfExtraContext) {
  StatsPrefixRecordingFactory recording_factory;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration(
      recording_factory);
  FilterChainFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
  filter_chain:
    filters:
    - name: stats-prefix-recording-filter
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.PassthroughFilterConfig
  )EOF";

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_string, *proto_config);

  const std::string stats_prefix = "http.ingress.";
  Server::Configuration::ExtraFactoryContext extra_context{
      ProtobufMessage::getNullValidationVisitor(), stats_prefix};
  EXPECT_TRUE(
      factory.createHttpFilterRouteConfig(*proto_config, factory_context, extra_context).ok());
  EXPECT_EQ("http.ingress.", recording_factory.stats_prefix_);
}

} // namespace
} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
