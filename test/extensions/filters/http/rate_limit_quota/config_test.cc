#include <memory>
#include <string>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/http/filter_factory.h"

#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include "test/extensions/filters/http/rate_limit_quota/client_test_utils.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

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

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  // Handle the global client's creation by expecting the underlying async grpc
  // client creation. getOrThrow fails otherwise.
  auto mock_stream_client = std::make_unique<RateLimitTestClient>();
  mock_stream_client->expectClientCreation();

  RateLimitQuotaFilterFactory factory;
  std::string stats_prefix = "test";
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProtoTyped(
      filter_config, stats_prefix, mock_stream_client->context_);
  cb(filter_callback);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
