#include <string>

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/router/router.h"
#include "source/extensions/filters/http/router/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {
namespace {

TEST(RouterFilterConfigTest, SimpleRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  start_child_span: true
  )EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, BadRouterFilterConfig) {
  const std::string yaml_string = R"EOF(
  dynamic_stats: true
  route: {}
  )EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException,
                          "route: Cannot find field");
}

TEST(RouterFilterConfigTest, RouterFilterWithUnsupportedStrictHeaderCheck) {
  const std::string yaml = R"EOF(
  strict_check_headers:
  - unsupportedHeader
  )EOF";

  envoy::extensions::filters::http::router::v3::Router router_config;
  TestUtility::loadFromYaml(yaml, router_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(router_config, "stats.", context),
      ProtoValidationException,
      "Proto constraint validation failed \\(RouterValidationError.StrictCheckHeaders");
}

TEST(RouterFilterConfigTest, RouterV2Filter) {
  envoy::extensions::filters::http::router::v3::Router router_config;
  router_config.mutable_dynamic_stats()->set_value(true);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(router_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, DefaultAllowedEarlyDataForSafeRequest) {
  envoy::extensions::filters::http::router::v3::Router proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) {
        auto& router = static_cast<Envoy::Router::Filter&>(*filter);
        Envoy::Http::TestRequestHeaderMapImpl get_request_headers{
            {":method", "GET"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(get_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl post_request_headers{
            {":method", "POST"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_FALSE(router.config().allowsEarlyDataForRequest(post_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl head_request_headers{
            {":method", "HEAD"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(head_request_headers));
      }));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, AllowEarlyDataForSafeRequest) {
  const std::string yaml_string = R"EOF(
allow_safe_requests: true
)EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) {
        auto& router = static_cast<Envoy::Router::Filter&>(*filter);
        Envoy::Http::TestRequestHeaderMapImpl get_request_headers{
            {":method", "GET"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(get_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl post_request_headers{
            {":method", "POST"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_FALSE(router.config().allowsEarlyDataForRequest(post_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl head_request_headers{
            {":method", "HEAD"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(head_request_headers));
      }));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, DisallowEarlyData) {
  const std::string yaml_string = R"EOF(
allow_safe_requests: false
)EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) {
        auto& router = static_cast<Envoy::Router::Filter&>(*filter);
        Envoy::Http::TestRequestHeaderMapImpl get_request_headers{
            {":method", "GET"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_FALSE(router.config().allowsEarlyDataForRequest(get_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl post_request_headers{
            {":method", "POST"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_FALSE(router.config().allowsEarlyDataForRequest(post_request_headers));
      }));
  cb(filter_callback);
}

TEST(RouterFilterConfigTest, ConfigAllowedEarlyDataRequest) {
  const std::string yaml_string = R"EOF(
early_data_options:
  allowed_methods: ["GET", "POST"]
)EOF";

  envoy::extensions::filters::http::router::v3::Router proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_))
      .WillOnce(Invoke([](Http::StreamDecoderFilterSharedPtr filter) {
        auto& router = static_cast<Envoy::Router::Filter&>(*filter);
        Envoy::Http::TestRequestHeaderMapImpl get_request_headers{
            {":method", "GET"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(get_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl post_request_headers{
            {":method", "POST"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_TRUE(router.config().allowsEarlyDataForRequest(post_request_headers));

        Envoy::Http::TestRequestHeaderMapImpl head_request_headers{
            {":method", "HEAD"},
            {":path", "/my/fake/path/content"},
            {":scheme", "http"},
            {":authority", "mydomain.com"}};
        EXPECT_FALSE(router.config().allowsEarlyDataForRequest(head_request_headers));
      }));
  cb(filter_callback);
}

} // namespace
} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
