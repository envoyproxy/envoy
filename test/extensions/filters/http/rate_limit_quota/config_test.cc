#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

TEST(RateLimitQuotaFilterConfigTest, RateLimitQuotaFilterWithCorrectProto) {
  std::string filter_config_yaml = R"EOF(
    http_uri:
      uri: http://test/path
      cluster: test_cluster
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  std::string stats_prefix = "test";
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, stats_prefix, context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
