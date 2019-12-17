#include <string>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/http/health_check/v2/health_check.pb.h"
#include "envoy/config/filter/http/health_check/v2/health_check.pb.validate.h"

#include "extensions/filters/http/health_check/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {
namespace {

TEST(HealthCheckFilterConfig, HealthCheckFilter) {
  const std::string yaml_string = R"EOF(
  pass_through_mode: true
  headers:
    - name: ":path"
      exact_match: "/hc"
  )EOF";

  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  HealthCheckFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HealthCheckFilterConfig, BadHealthCheckFilterConfig) {
  const std::string yaml_string = R"EOF(
  pass_through_mode: true
  headers:
    - name: ":path"
      exact_match: "/hc"
  status: 500
  )EOF";

  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException,
                          "status: Cannot find field");
}

TEST(HealthCheckFilterConfig, FailsWhenNotPassThroughButTimeoutSetYaml) {
  const std::string yaml_string = R"EOF(
  pass_through_mode: false
  cache_time: 0.234s
  headers:
    - name: ":path"
      exact_match: "/foo"
  )EOF";

  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  HealthCheckFilterConfig factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "dummy_stats_prefix", context),
               EnvoyException);
}

TEST(HealthCheckFilterConfig, NotFailingWhenNotPassThroughAndTimeoutNotSetYaml) {
  const std::string yaml_string = R"EOF(
  pass_through_mode: true
  cache_time: 0.234s
  headers:
    - name: ":path"
      exact_match: "/foo"
  )EOF";

  envoy::config::filter::http::health_check::v2::HealthCheck proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);

  HealthCheckFilterConfig factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_NO_THROW(
      factory.createFilterFactoryFromProto(proto_config, "dummy_stats_prefix", context));
}

TEST(HealthCheckFilterConfig, FailsWhenNotPassThroughButTimeoutSetProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::config::filter::http::health_check::v2::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  config.mutable_cache_time()->set_seconds(10);
  envoy::api::v2::route::HeaderMatcher& header = *config.add_headers();
  header.set_name(":path");
  header.set_exact_match("foo");

  EXPECT_THROW(
      healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context),
      EnvoyException);
}

TEST(HealthCheckFilterConfig, NotFailingWhenNotPassThroughAndTimeoutNotSetProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::config::filter::http::health_check::v2::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  envoy::api::v2::route::HeaderMatcher& header = *config.add_headers();
  header.set_name(":path");
  header.set_exact_match("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}

TEST(HealthCheckFilterConfig, HealthCheckFilterWithEmptyProto) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::config::filter::http::health_check::v2::HealthCheck config =
      *dynamic_cast<envoy::config::filter::http::health_check::v2::HealthCheck*>(
          healthCheckFilterConfig.createEmptyConfigProto().get());

  config.mutable_pass_through_mode()->set_value(false);
  envoy::api::v2::route::HeaderMatcher& header = *config.add_headers();
  header.set_name(":path");
  header.set_exact_match("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}

void testHealthCheckHeaderMatch(
    const envoy::config::filter::http::health_check::v2::HealthCheck& input_config,
    Http::TestHeaderMapImpl& input_headers, bool expect_health_check_response) {
  HealthCheckFilterConfig healthCheckFilterConfig;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ProtobufTypes::MessagePtr config_msg = healthCheckFilterConfig.createEmptyConfigProto();
  auto config =
      dynamic_cast<envoy::config::filter::http::health_check::v2::HealthCheck*>(config_msg.get());
  ASSERT_NE(config, nullptr);

  *config = input_config;

  Http::FilterFactoryCb cb =
      healthCheckFilterConfig.createFilterFactoryFromProto(*config, "dummy_stats_prefix", context);

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  Http::StreamFilterSharedPtr health_check_filter;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_))
      .WillRepeatedly(Invoke([&health_check_filter](Http::StreamFilterSharedPtr filter) {
        health_check_filter = filter;
      }));

  cb(filter_callbacks);
  ASSERT_NE(health_check_filter, nullptr);

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  health_check_filter->setDecoderFilterCallbacks(decoder_callbacks);

  if (expect_health_check_response) {
    // Expect that the filter intercepts this request because all headers match.
    Http::TestHeaderMapImpl health_check_response{{":status", "200"}};
    EXPECT_CALL(decoder_callbacks, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(health_check_filter->decodeHeaders(input_headers, true),
              Http::FilterHeadersStatus::StopIteration);
  } else {
    EXPECT_EQ(health_check_filter->decodeHeaders(input_headers, true),
              Http::FilterHeadersStatus::Continue);
  }
}

// Basic header match with two conditions should match if both conditions are satisfied.
TEST(HealthCheckFilterConfig, HealthCheckFilterHeaderMatch) {
  envoy::config::filter::http::health_check::v2::HealthCheck config;

  config.mutable_pass_through_mode()->set_value(false);

  envoy::api::v2::route::HeaderMatcher& xheader = *config.add_headers();
  xheader.set_name("x-healthcheck");

  envoy::api::v2::route::HeaderMatcher& yheader = *config.add_headers();
  yheader.set_name("y-healthcheck");
  yheader.set_exact_match("foo");

  Http::TestHeaderMapImpl headers{{"x-healthcheck", "arbitrary_value"}, {"y-healthcheck", "foo"}};

  testHealthCheckHeaderMatch(config, headers, true);
}

// The match should fail if a single header value fails to match.
TEST(HealthCheckFilterConfig, HealthCheckFilterHeaderMatchWrongValue) {
  envoy::config::filter::http::health_check::v2::HealthCheck config;

  config.mutable_pass_through_mode()->set_value(false);

  envoy::api::v2::route::HeaderMatcher& xheader = *config.add_headers();
  xheader.set_name("x-healthcheck");

  envoy::api::v2::route::HeaderMatcher& yheader = *config.add_headers();
  yheader.set_name("y-healthcheck");
  yheader.set_exact_match("foo");

  Http::TestHeaderMapImpl headers{{"x-healthcheck", "arbitrary_value"}, {"y-healthcheck", "bar"}};

  testHealthCheckHeaderMatch(config, headers, false);
}

// If either of the specified headers is completely missing the match should fail.
TEST(HealthCheckFilterConfig, HealthCheckFilterHeaderMatchMissingHeader) {
  envoy::config::filter::http::health_check::v2::HealthCheck config;

  config.mutable_pass_through_mode()->set_value(false);

  envoy::api::v2::route::HeaderMatcher& xheader = *config.add_headers();
  xheader.set_name("x-healthcheck");

  envoy::api::v2::route::HeaderMatcher& yheader = *config.add_headers();
  yheader.set_name("y-healthcheck");
  yheader.set_exact_match("foo");

  Http::TestHeaderMapImpl headers{{"y-healthcheck", "foo"}};

  testHealthCheckHeaderMatch(config, headers, false);
}

// Conditions for the same header should match if they are both satisfied.
TEST(HealthCheckFilterConfig, HealthCheckFilterDuplicateMatch) {
  envoy::config::filter::http::health_check::v2::HealthCheck config;

  config.mutable_pass_through_mode()->set_value(false);

  envoy::api::v2::route::HeaderMatcher& header = *config.add_headers();
  header.set_name("x-healthcheck");
  header.set_exact_match("foo");

  envoy::api::v2::route::HeaderMatcher& dup_header = *config.add_headers();
  dup_header.set_name("x-healthcheck");

  Http::TestHeaderMapImpl headers{{"x-healthcheck", "foo"}};

  testHealthCheckHeaderMatch(config, headers, true);
}

// Conditions on the same header should not match if one or more is not satisfied.
TEST(HealthCheckFilterConfig, HealthCheckFilterDuplicateNoMatch) {
  envoy::config::filter::http::health_check::v2::HealthCheck config;

  config.mutable_pass_through_mode()->set_value(false);

  envoy::api::v2::route::HeaderMatcher& header = *config.add_headers();
  header.set_name("x-healthcheck");
  header.set_exact_match("foo");

  envoy::api::v2::route::HeaderMatcher& dup_header = *config.add_headers();
  dup_header.set_name("x-healthcheck");
  dup_header.set_exact_match("bar");

  Http::TestHeaderMapImpl headers{{"x-healthcheck", "foo"}};

  testHealthCheckHeaderMatch(config, headers, false);
}

} // namespace
} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
