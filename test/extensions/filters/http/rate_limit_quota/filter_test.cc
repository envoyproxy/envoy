#include <initializer_list>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using ::Envoy::Extensions::HttpFilters::RateLimitQuota::FilterConfig;
using ::Envoy::StatusHelpers::StatusIs;
using Server::Configuration::MockFactoryContext;
using ::testing::NiceMock;

constexpr char ValidMatcherConfig[] = R"EOF(
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
                "environment":
                    custom_value:
                      name: "test_1"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: environment
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: group
            reporting_interval: 60s
  )EOF";

const std::string GoogleGrpcConfig = R"EOF(
  rlqs_server:
    google_grpc:
      target_uri: rate_limit_quota_server
      stat_prefix: google
  )EOF";

// const std::string GrpcConfig = R"EOF(
//   rlqs_server:
//     envoy_grpc:
//       cluster_name: "rate_limit_quota_server"
//   )EOF";

// TODO(tyxia) CEL matcher config to be used later.
// constexpr char CelMatcherConfig[] = R"EOF(
//     matcher_list:
//       matchers:
//         # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
//         predicate:
//           single_predicate:
//             input:
//               typed_config:
//                 "@type": type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput
//                 header_name: environment
//             custom_match:
//               typed_config:
//                 '@type': type.googleapis.com/xds.type.matcher.v3.CelMatcher
//                 expr_match:
//                   # Shortened for illustration purposes. Here should be parsed CEL expression:
//                   # request.headers['user_group'] == 'admin'
//                   parsed_expr: {}
//         on_match:
//           action:
//             name: rate_limit_quota
//             typed_config:
//               "@type":
//               type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
//               bucket_id_builder:
//                 bucket_id_builder:
//                   "name":
//                       string_value: "prod"
//   )EOF";

class FilterTest : public testing::Test {
public:
  FilterTest() {
    // Add the grpc service config.
    TestUtility::loadFromYaml(GoogleGrpcConfig, config_);
  }

  void addMatcherConfigAndCreateFilter(bool valid) {
    // Add the matcher configuration. Invalid bucket_matcher configuration will be just empty
    // matcher config.
    if (valid) {
      xds::type::matcher::v3::Matcher matcher;
      TestUtility::loadFromYaml(ValidMatcherConfig, matcher);
      config_.mutable_bucket_matchers()->MergeFrom(matcher);
    }

    filter_config_ = std::make_shared<FilterConfig>(config_);
    filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_, nullptr);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;

  std::unique_ptr<RateLimitQuotaFilter> filter_;
  FilterConfigConstSharedPtr filter_config_;
  FilterConfig config_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(FilterTest, InvalidBucketMatcherConfig) {
  addMatcherConfigAndCreateFilter(/*valid=*/false);
  auto match = filter_->requestMatching(default_headers_);
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match.status().message(), "Matcher has not been initialized yet");
}

TEST_F(FilterTest, BuildBucketSettingsSucceeded) {
  addMatcherConfigAndCreateFilter(/*valid=*/true);
  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  // Http::TestRequestHeaderMapImpl headers = default_headers_;
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  // Add custom_value_pairs to the request header for exact value_match in the predicate.
  for (auto const& pair : custom_value_pairs) {
    headers.addCopy(pair.first, pair.second);
  }

  // The expected bucket ids has one additional pair that is built statically via `string_value`
  // from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});

  // Perform request matching and get the generated bucket ids if matched.
  auto match = filter_->requestMatching(headers);
  EXPECT_TRUE(match.ok());
  auto bucket_ids = match.value().bucket();

  // Serialize the proto map to std map for comparison. We can avoid this conversion by using
  // `EqualsProto()` directly once it is available in the Envoy code base.
  auto serialized_bucket_ids =
      absl::flat_hash_map<std::string, std::string>(bucket_ids.begin(), bucket_ids.end());
  EXPECT_THAT(expected_bucket_ids,
              testing::UnorderedPointwise(testing::Eq(), serialized_bucket_ids));
}

TEST_F(FilterTest, BuildBucketSettingsFailed) {
  addMatcherConfigAndCreateFilter(/*valid=*/true);
  // Define the wrong input that doesn't match the values in the config: it has `{"env", "staging"}`
  // rather than `{"environment", "staging"}`.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"env", "staging"},
                                                                      {"group", "envoy"}};
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  for (auto const& pair : custom_value_pairs) {
    default_headers_.addCopy(pair.first, pair.second);
  }

  // The expected bucket ids has one additional pair that is built via `string_value` static method
  // from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});

  // Perform request matching and matching is expected to fail due to wrong inputs provided by
  // `custom_value_pairs` above.
  auto match = filter_->requestMatching(headers);
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match.status().message(), "Failed to match the request");
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
