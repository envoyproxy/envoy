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
  rlqs_server:
    envoy_grpc:
      cluster_name: "rate_limit_quota_server"
  domain: test
  bucket_matchers:
    matcher_list:
      matchers:
        # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
        predicate:
          single_predicate:
            input:
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: environment
            value_match:
              exact: staging
        on_match:
          action:
            name: rate_limit_quota
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
              bucket_id_builder:
                bucket_id_builder:
                  "name":
                      string_value: "prod"
              reporting_interval: 60s
  )EOF";
  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RateLimitQuotaFilterFactory factory;
  std::string stats_prefix = "test";
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoTyped(filter_config, stats_prefix, context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
