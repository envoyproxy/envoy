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
#include "xds/type/matcher/v3/cel.pb.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

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

constexpr char InvalidMatcherConfig[] = R"EOF(
  matcher_list:
    matchers:
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
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        # No value is defined here, it will cause the failure of generation of bucket id.
                        header_name:
            reporting_interval: 60s
  )EOF";

constexpr char OnNoMatchConfig[] = R"EOF(
  matcher_list:
    matchers:
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
                # The on_match field here will not be matched by the request header.
                "NO_MATCHED_NAME":
                    string_value: "NO_MATCHED"
            reporting_interval: 60s
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "on_no_match_name":
                string_value: "on_no_match_value"
            "on_no_match_name_2":
                string_value: "on_no_match_value_2"
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

// No matcher type (matcher_list or matcher_tree) is configure here. It will read from `on_no_match`
// field directly.
constexpr char OnNoMatchConfigWithNoMatcher[] = R"EOF(
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "on_no_match_name":
                string_value: "on_no_match_value"
            "on_no_match_name_2":
                string_value: "on_no_match_value_2"
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

// By design, on_no_match here only supports static bucket_id generation (via string value) and
// doesn't support dynamic way (via custom_value extension). So the configuration with
// `custom_value` typed_config is invalid configuration that will cause the failure of generating
// the bucket id.
constexpr char InvalidOnNoMatcherConfig[] = R"EOF(
  matcher_list:
    matchers:
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      # Here is on_match field that will not be matched by the request header.
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "NO_MATCHED_NAME":
                    string_value: "NO_MATCHED"
            reporting_interval: 60s
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "environment":
                custom_value:
                  name: "test_1"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: environment
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
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

enum class MatcherConfigType {
  Valid,
  Invalid,
  Empty,
  NoMatcher,
  ValidOnNoMatchConfig,
  InvalidOnNoMatchConfig
};

class FilterTest : public testing::Test {
public:
  FilterTest() {
    // Add the grpc service config.
    TestUtility::loadFromYaml(GoogleGrpcConfig, config_);
  }

  ~FilterTest() override { filter_->onDestroy(); }

  void addMatcherConfig(MatcherConfigType config_type) {

    // Add the matcher configuration.
    xds::type::matcher::v3::Matcher matcher;
    switch (config_type) {
    case MatcherConfigType::Valid: {
      TestUtility::loadFromYaml(ValidMatcherConfig, matcher);
      break;
    }
    case MatcherConfigType::ValidOnNoMatchConfig: {
      TestUtility::loadFromYaml(OnNoMatchConfig, matcher);
      break;
    }
    case MatcherConfigType::Invalid: {
      TestUtility::loadFromYaml(InvalidMatcherConfig, matcher);
      break;
    }
    case MatcherConfigType::InvalidOnNoMatchConfig: {
      TestUtility::loadFromYaml(InvalidOnNoMatcherConfig, matcher);
      break;
    }
    case MatcherConfigType::NoMatcher: {
      TestUtility::loadFromYaml(OnNoMatchConfigWithNoMatcher, matcher);
      break;
    }
    case MatcherConfigType::Empty:
    default:
      break;
    }

    // Empty matcher config will not have the bucket matcher configured.
    if (config_type != MatcherConfigType::Empty) {
      config_.mutable_bucket_matchers()->MergeFrom(matcher);
    }
  }

  void createFilter(bool set_callback = true) {
    filter_config_ = std::make_shared<FilterConfig>(config_);
    filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_);
    if (set_callback) {
      filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    }
  }

  void constructMismatchedRequestHeader() {
    // Define the wrong input that doesn't match the values in the config: it has `{"env",
    // "staging"}` rather than `{"environment", "staging"}`.
    absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"env", "staging"},
                                                                        {"group", "envoy"}};

    // Add custom_value_pairs to the request header for exact value_match in the predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  void buildCustomHeader(const absl::flat_hash_map<std::string, std::string>& custom_value_pairs) {
    // Add custom_value_pairs to the request header for exact value_match in the predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  void verifyRequestMatchingSucceeded(
      const absl::flat_hash_map<std::string, std::string>& expected_bucket_ids) {
    // Perform request matching.
    auto match_result = filter_->requestMatching(default_headers_);
    // Asserts that the request matching succeeded.
    // OK status is expected to be returned even if the exact request matching failed. It is because
    // `on_no_match` field is configured.
    ASSERT_TRUE(match_result.ok());
    // Retrieve the matched action.
    const RateLimitOnMatchAction* match_action =
        dynamic_cast<RateLimitOnMatchAction*>(match_result.value().get());

    RateLimitQuotaValidationVisitor visitor = {};
    // Generate the bucket ids.
    auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
    // Asserts that the bucket id generation succeeded and then retrieve the bucket ids.
    ASSERT_TRUE(ret.ok());
    auto bucket_ids = ret.value().bucket();
    auto serialized_bucket_ids =
        absl::flat_hash_map<std::string, std::string>(bucket_ids.begin(), bucket_ids.end());
    // Verifies that the expected bucket ids are generated for `on_no_match` case.
    EXPECT_THAT(expected_bucket_ids,
                testing::UnorderedPointwise(testing::Eq(), serialized_bucket_ids));
  }

  NiceMock<MockFactoryContext> context_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;

  std::unique_ptr<RateLimitQuotaFilter> filter_;
  FilterConfigConstSharedPtr filter_config_;
  FilterConfig config_;
  Http::TestRequestHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(FilterTest, EmptyMatcherConfig) {
  addMatcherConfig(MatcherConfigType::Empty);
  createFilter();
  auto match_result = filter_->requestMatching(default_headers_);
  EXPECT_FALSE(match_result.ok());
  EXPECT_THAT(match_result, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match_result.status().message(), "Matcher tree has not been initialized yet.");
}

TEST_F(FilterTest, RequestMatchingSucceeded) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};

  buildCustomHeader(custom_value_pairs);

  // The expected bucket ids has one additional pair that is built statically via `string_value`
  // from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse resp;
  filter_->onQuotaResponse(resp);
}

TEST_F(FilterTest, RequestMatchingFailed) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  constructMismatchedRequestHeader();

  // Perform request matching.
  auto match = filter_->requestMatching(default_headers_);
  // Not_OK status is expected to be returned because the matching failed due to mismatched inputs.
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kNotFound));
  EXPECT_EQ(match.status().message(), "Matching completed but no match result was found.");
}

TEST_F(FilterTest, RequestMatchingFailedWithEmptyHeader) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  Http::TestRequestHeaderMapImpl empty_header = {};
  // Perform request matching.
  auto match = filter_->requestMatching(empty_header);
  // Not_OK status is expected to be returned because the matching failed due to empty headers.
  EXPECT_FALSE(match.ok());
  EXPECT_EQ(match.status().message(),
            "Unable to match due to the required data not being available.");
}

TEST_F(FilterTest, RequestMatchingFailedWithNoCallback) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter(/*set_callback*/ false);

  auto match = filter_->requestMatching(default_headers_);
  EXPECT_FALSE(match.ok());
  EXPECT_THAT(match, StatusIs(absl::StatusCode::kInternal));
  EXPECT_EQ(match.status().message(), "Filter callback has not been initialized successfully yet.");
}

TEST_F(FilterTest, RequestMatchingWithOnNoMatch) {
  addMatcherConfig(MatcherConfigType::ValidOnNoMatchConfig);
  createFilter();
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = {
      {"on_no_match_name", "on_no_match_value"}, {"on_no_match_name_2", "on_no_match_value_2"}};
  verifyRequestMatchingSucceeded(expected_bucket_ids);
}

TEST_F(FilterTest, RequestMatchingOnNoMatchWithNoMatcher) {
  addMatcherConfig(MatcherConfigType::NoMatcher);
  createFilter();
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = {
      {"on_no_match_name", "on_no_match_value"}, {"on_no_match_name_2", "on_no_match_value_2"}};
  verifyRequestMatchingSucceeded(expected_bucket_ids);
}

TEST_F(FilterTest, RequestMatchingWithInvalidOnNoMatch) {
  addMatcherConfig(MatcherConfigType::InvalidOnNoMatchConfig);
  createFilter();

  // Perform request matching.
  auto match_result = filter_->requestMatching(default_headers_);
  // Asserts that the request matching succeeded.
  // OK status is expected to be returned even if the exact request matching failed. It is because
  // `on_no_match` field is configured.
  ASSERT_TRUE(match_result.ok());
  // Retrieve the matched action.
  const RateLimitOnMatchAction* match_action =
      dynamic_cast<RateLimitOnMatchAction*>(match_result.value().get());

  RateLimitQuotaValidationVisitor visitor = {};
  // Generate the bucket ids.
  auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
  // Bucket id generation is expected to fail, which is due to no support for dynamic id generation
  // (i.e., via custom_value with for on_no_match case.
  EXPECT_FALSE(ret.ok());
  EXPECT_EQ(ret.status().message(), "Failed to generate the id from custom value config.");
}

TEST_F(FilterTest, DecodeHeaderWithValidConfig) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();

  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};

  buildCustomHeader(custom_value_pairs);

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithInValidConfig) {
  addMatcherConfig(MatcherConfigType::Invalid);
  createFilter();

  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);
  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithOnNoMatchConfigured) {
  addMatcherConfig(MatcherConfigType::ValidOnNoMatchConfig);
  createFilter();

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithEmptyConfig) {
  addMatcherConfig(MatcherConfigType::Empty);
  createFilter();
  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithMismatchHeader) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  constructMismatchedRequestHeader();

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, RequestMatchingSucceededWithCelMatcher) {
  // Compiled CEL expression string: request.headers['authenticated_user'] == 'staging'
  std::string cel_expr_str = R"pb(
    expr {
      id: 8
      call_expr {
        function: "_==_"
        args {
          id: 6
          call_expr {
            function: "_[_]"
            args {
              id: 5
              select_expr {
                operand {
                  id: 4
                  ident_expr {name: "request"}
                }
                field: "headers"
              }
            }
            args {
              id: 7
              const_expr {
                string_value: "authenticated_user"
              }
            }
          }
        }
        args {
          id: 9
          const_expr { string_value: "staging" }
        }
      }
    }
  )pb";
  google::api::expr::v1alpha1::CheckedExpr checked_expr;
  Protobuf::TextFormat::ParseFromString(cel_expr_str, &checked_expr);

  xds::type::matcher::v3::CelMatcher cel_matcher;
  cel_matcher.mutable_expr_match()->mutable_checked_expr()->MergeFrom(checked_expr);
  xds::type::matcher::v3::Matcher matcher;

  auto* inner_matcher = matcher.mutable_matcher_list()->add_matchers();
  auto* single_predicate = inner_matcher->mutable_predicate()->mutable_single_predicate();

  xds::type::matcher::v3::HttpAttributesCelMatchInput cel_match_input;
  single_predicate->mutable_input()->set_name("envoy.matching.inputs.cel_data_input");
  single_predicate->mutable_input()->mutable_typed_config()->PackFrom(cel_match_input);

  auto* custom_matcher = single_predicate->mutable_custom_match();
  custom_matcher->mutable_typed_config()->PackFrom(cel_matcher);

  std::string on_match_str = R"pb(
    action {
      name: "rate_limit_quota"
      typed_config {
        [type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings] {
          bucket_id_builder {
            bucket_id_builder {
              key: "authenticated_user"
              value {
                custom_value {
                  name: "test_1"
                  typed_config {
                    [type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput] {
                      header_name: "authenticated_user"
                    }
                  }
                }
              }
            }
            bucket_id_builder {
              key: "name"
              value {
                string_value: "prod"
              }
            }
          }
          reporting_interval {
            seconds: 60
          }
        }
      }
    }
  )pb";
  xds::type::matcher::v3::Matcher::OnMatch on_match;
  Protobuf::TextFormat::ParseFromString(on_match_str, &on_match);
  inner_matcher->mutable_on_match()->MergeFrom(on_match);
  config_.mutable_bucket_matchers()->MergeFrom(matcher);
  std::cout << config_.DebugString() << std::endl;
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically via `custom_value`
  // in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {
      {"authenticated_user", "staging"}};

  buildCustomHeader(custom_value_pairs);

  // The expected bucket ids has one additional pair that is built statically via `string_value`
  // from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse resp;
  filter_->onQuotaResponse(resp);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
