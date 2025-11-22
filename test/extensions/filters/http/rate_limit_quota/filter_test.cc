#include <atomic>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "test/extensions/filters/http/rate_limit_quota/client_test_utils.h"
#include "test/extensions/filters/http/rate_limit_quota/test_utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
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
using envoy::service::rate_limit_quota::v3::BucketId;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using ::Envoy::StatusHelpers::StatusIs;
using envoy::type::v3::RateLimitStrategy;
using envoy::type::v3::TokenBucket;
using Server::Configuration::MockFactoryContext;
using ::testing::NiceMock;

enum class MatcherConfigType {
  Valid,
  ValidPreview,
  Invalid,
  Empty,
  NoMatcher,
  ValidOnNoMatchConfig,
  InvalidOnNoMatchConfig
};

class FilterTest : public testing::Test {
public:
  FilterTest() {
    // Enable keep_matching support for preview matcher testing.
    visitor_.setSupportKeepMatching(true);
    // Add the grpc service config.
    TestUtility::loadFromYaml(std::string(GoogleGrpcConfig), config_);
  }

  void addMatcherConfig(xds::type::matcher::v3::Matcher& matcher) {
    config_.mutable_bucket_matchers()->MergeFrom(matcher);
    match_tree_ = matcher_factory_.create(matcher)();
    ASSERT_TRUE(visitor_.errors().empty()) << "First error: " << visitor_.errors().at(0);
  }

  void addMatcherConfig(MatcherConfigType config_type) {
    // Add the matcher configuration.
    xds::type::matcher::v3::Matcher matcher;
    switch (config_type) {
    case MatcherConfigType::Valid: {
      TestUtility::loadFromYaml(std::string(ValidMatcherConfig), matcher);
      break;
    }
    case MatcherConfigType::ValidPreview: {
      TestUtility::loadFromYaml(std::string(ValidPreviewMatcherConfig), matcher);
      break;
    }
    case MatcherConfigType::ValidOnNoMatchConfig: {
      TestUtility::loadFromYaml(std::string(OnNoMatchConfig), matcher);
      break;
    }
    case MatcherConfigType::Invalid: {
      TestUtility::loadFromYaml(std::string(InvalidMatcherConfig), matcher);
      break;
    }
    case MatcherConfigType::InvalidOnNoMatchConfig: {
      TestUtility::loadFromYaml(std::string(InvalidOnNoMatcherConfig), matcher);
      break;
    }
    case MatcherConfigType::NoMatcher: {
      TestUtility::loadFromYaml(std::string(OnNoMatchConfigWithNoMatcher), matcher);
      break;
    }
    case MatcherConfigType::Empty:
    default:
      break;
    }

    // Empty matcher config will not have the bucket matcher configured.
    if (config_type != MatcherConfigType::Empty) {
      addMatcherConfig(matcher);
    }
  }

  void createFilter(bool set_callback = true) {
    filter_config_ = std::make_shared<FilterConfig>(config_);
    Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
        Grpc::GrpcServiceConfigWithHashKey(filter_config_->rlqs_server());

    mock_local_client_ = new MockRateLimitClient();
    filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_,
                                                     absl::WrapUnique(mock_local_client_),
                                                     config_with_hash_key, match_tree_);
    if (set_callback) {
      filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    }
  }

  void constructMismatchedRequestHeader() {
    // Define the wrong input that doesn't match the values in the config: it
    // has `{"env", "staging"}` rather than `{"environment", "staging"}`.
    absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"env", "staging"},
                                                                        {"group", "envoy"}};

    // Add custom_value_pairs to the request header for exact value_match in the
    // predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  void buildCustomHeader(const absl::flat_hash_map<std::string, std::string>& custom_value_pairs) {
    // Add custom_value_pairs to the request header for exact value_match in the
    // predicate.
    for (auto const& pair : custom_value_pairs) {
      default_headers_.addCopy(pair.first, pair.second);
    }
  }

  void verifyRequestMatchingSucceeded(
      const absl::flat_hash_map<std::string, std::string>& expected_bucket_ids) {
    // Perform request matching.
    auto match_result = filter_->requestMatching(default_headers_);
    // Asserts that the request matching succeeded.
    // OK status is expected to be returned even if the exact request matching
    // failed. It is because `on_no_match` field is configured.
    ASSERT_TRUE(match_result.ok());
    // Retrieve the matched action.
    const RateLimitOnMatchAction* match_action =
        dynamic_cast<const RateLimitOnMatchAction*>(match_result.value().get());

    RateLimitQuotaValidationVisitor visitor = {};
    // Generate the bucket ids.
    auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
    // Asserts that the bucket id generation succeeded and then retrieve the
    // bucket ids.
    ASSERT_TRUE(ret.ok());
    auto bucket_ids = ret.value().bucket();
    auto serialized_bucket_ids =
        absl::flat_hash_map<std::string, std::string>(bucket_ids.begin(), bucket_ids.end());
    // Verifies that the expected bucket ids are generated for `on_no_match`
    // case.
    EXPECT_THAT(expected_bucket_ids,
                testing::UnorderedPointwise(testing::Eq(), serialized_bucket_ids));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockFactoryContext> context_;
  RateLimitOnMatchActionContext action_context_ = {};
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext>
      matcher_factory_ =
          Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext>(
              action_context_, context_.serverFactoryContext(), visitor_);
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;

  MockRateLimitClient* mock_local_client_ = nullptr;
  FilterConfigConstSharedPtr filter_config_;
  FilterConfig config_;
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_ = nullptr;
  std::unique_ptr<RateLimitQuotaFilter> filter_;
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
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};

  buildCustomHeader(custom_value_pairs);

  // The expected bucket ids has one additional pair that is built statically
  // via `string_value` from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);
}

TEST_F(FilterTest, RequestMatchingFailed) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  constructMismatchedRequestHeader();

  // Perform request matching.
  auto match = filter_->requestMatching(default_headers_);
  // Not_OK status is expected to be returned because the matching failed due to
  // mismatched inputs.
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
  // Not_OK status is expected to be returned because the matching failed due to
  // empty headers.
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
  // OK status is expected to be returned even if the exact request matching
  // failed. It is because `on_no_match` field is configured.
  ASSERT_TRUE(match_result.ok());
  // Retrieve the matched action.
  const RateLimitOnMatchAction* match_action =
      dynamic_cast<const RateLimitOnMatchAction*>(match_result.value().get());

  RateLimitQuotaValidationVisitor visitor = {};
  // Generate the bucket ids.
  auto ret = match_action->generateBucketId(filter_->matchingData(), context_, visitor);
  // Bucket id generation is expected to fail, which is due to no support for
  // dynamic id generation (i.e., via custom_value with for on_no_match case.
  EXPECT_FALSE(ret.ok());
  EXPECT_EQ(ret.status().message(), "Failed to generate the id from custom value config.");
}

TEST_F(FilterTest, DecodeHeaderWithInValidConfig) {
  addMatcherConfig(MatcherConfigType::Invalid);
  createFilter();

  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);
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
  // Compiled CEL expression string: request.headers['authenticated_user'] ==
  // 'staging'
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
                  ident_expr { name: "request" }
                }
                field: "headers"
              }
            }
            args {
              id: 7
              const_expr { string_value: "authenticated_user" }
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
        [type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3
             .RateLimitQuotaBucketSettings] {
          bucket_id_builder {
            bucket_id_builder {
              key: "authenticated_user"
              value {
                custom_value {
                  name: "test_1"
                  typed_config {
                    [type.googleapis.com/
                     envoy.type.matcher.v3.HttpRequestHeaderMatchInput] {
                      header_name: "authenticated_user"
                    }
                  }
                }
              }
            }
            bucket_id_builder {
              key: "name"
              value { string_value: "prod" }
            }
          }
          reporting_interval { seconds: 60 }
        }
      }
    }
  )pb";
  xds::type::matcher::v3::Matcher::OnMatch on_match;
  Protobuf::TextFormat::ParseFromString(on_match_str, &on_match);
  inner_matcher->mutable_on_match()->MergeFrom(on_match);
  config_.mutable_bucket_matchers()->MergeFrom(matcher);
  addMatcherConfig(matcher);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {
      {"authenticated_user", "staging"}};

  buildCustomHeader(custom_value_pairs);

  // The expected bucket ids has one additional pair that is built statically
  // via `string_value` from the config.
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);
}

BucketId bucketIdFromMap(const absl::flat_hash_map<std::string, std::string>& bucket_ids_map) {
  BucketId bucket_id;
  for (const auto& [k, v] : bucket_ids_map) {
    bucket_id.mutable_bucket()->insert({k, v});
  }
  return bucket_id;
}

TEST_F(FilterTest, DecodeHeaderWithValidOnNoMatchDenyWithSettings) {
  addMatcherConfig(MatcherConfigType::ValidOnNoMatchConfig);
  createFilter();
  constructMismatchedRequestHeader();

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids({
      {"on_no_match_name", "on_no_match_value"},
      {"on_no_match_name_2", "on_no_match_value_2"},
  });
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);
  *no_assignment_action.mutable_bucket_id() = bucket_id;

  // Default behavior is set to deny with custom settings in the bucket
  // matcher's `no_assignment_behavior` & `deny_response_settings`.
  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(nullptr));
  EXPECT_CALL(*mock_local_client_, createBucket(ProtoEqIgnoreRepeatedFieldOrdering(bucket_id),
                                                bucket_id_hash, ProtoEq(no_assignment_action), _,
                                                std::chrono::milliseconds::zero(), false))
      .WillOnce(Return());

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
}

TEST_F(FilterTest, DecodeHeadersWithoutCachedAssignment) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find none, and
  // go through bucket creation.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);

  // Expect the new bucket to fallback to ALLOW_ALL without a configured
  // no_assignment_behavior.
  BucketAction expected_action;
  expected_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  *expected_action.mutable_bucket_id() = bucket_id;

  // The bucket creation shouldn't try to include a fallback or
  // no-assignment-default action as neither is set in the BucketMatcher.
  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(nullptr));
  EXPECT_CALL(*mock_local_client_,
              createBucket(ProtoEqIgnoreRepeatedFieldOrdering(bucket_id), bucket_id_hash,
                           ProtoEq(expected_action), testing::IsNull(),
                           std::chrono::milliseconds::zero(), true))
      .WillOnce(Return());

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(FilterTest, DecodeHeaderWithCachedAllow) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // ALLOW_ALL blanket action.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->set_blanket_rule(
      RateLimitStrategy::ALLOW_ALL);
  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 2);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 0);
}

TEST_F(FilterTest, DecodeHeaderWithCachedDeny) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // ALLOW_ALL blanket action.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->set_blanket_rule(
      RateLimitStrategy::DENY_ALL);
  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 1);
}

TEST_F(FilterTest, DecodeHeaderWithTokenBucketAllow) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // ALLOW_ALL blanket action.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  TokenBucket* token_bucket = cached_action->mutable_quota_assignment_action()
                                  ->mutable_rate_limit_strategy()
                                  ->mutable_token_bucket();
  token_bucket->set_max_tokens(100);
  token_bucket->mutable_tokens_per_fill()->set_value(100);
  token_bucket->mutable_fill_interval()->set_seconds(60);
  // 100 available tokens so the test doesn't get throttled.
  std::shared_ptr<AtomicTokenBucketImpl> token_bucket_limiter =
      std::make_shared<AtomicTokenBucketImpl>(100, dispatcher_.timeSource(), 100 / 60);

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      token_bucket_limiter);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 2);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 0);
}

TEST_F(FilterTest, DecodeHeaderWithTokenBucketDeny) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // ALLOW_ALL blanket action.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  TokenBucket* token_bucket = cached_action->mutable_quota_assignment_action()
                                  ->mutable_rate_limit_strategy()
                                  ->mutable_token_bucket();
  token_bucket->set_max_tokens(1);
  token_bucket->mutable_tokens_per_fill()->set_value(1);
  token_bucket->mutable_fill_interval()->set_seconds(60);
  std::shared_ptr<AtomicTokenBucketImpl> token_bucket_limiter =
      std::make_shared<AtomicTokenBucketImpl>(1, dispatcher_.timeSource(), 1 / 60);
  // All subsequent requests should deny for 60 (mock) seconds.
  EXPECT_TRUE(token_bucket_limiter->consume());

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      token_bucket_limiter);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 1);
}

TEST_F(FilterTest, DecodeHeaderWithPreviewBucket) {
  addMatcherConfig(MatcherConfigType::ValidPreview);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({{"name", "prod"}});
  absl::flat_hash_map<std::string, std::string> expected_preview_bucket_ids(expected_bucket_ids);
  // The low priority config has a different bucket id intentionally.
  expected_preview_bucket_ids.insert({{"preview_name", "preview_test"}});

  // Expect request processing to check for an existing bucket, find none, and
  // go through bucket creation for the preview bucket.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  BucketId preview_bucket_id = bucketIdFromMap(expected_preview_bucket_ids);
  size_t preview_bucket_id_hash = MessageUtil::hash(preview_bucket_id);

  // Expect the new actionable bucket to fallback to DENY_ALL without a
  // configured no_assignment_behavior & the preview bucket to fallback to
  // ALLOW_ALL.
  BucketAction expected_action;
  expected_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);
  *expected_action.mutable_bucket_id() = bucket_id;
  BucketAction preview_expected_action;
  preview_expected_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  *preview_expected_action.mutable_bucket_id() = preview_bucket_id;

  // The bucket creation shouldn't try to include a fallback or
  // no-assignment-default action as neither is set in the BucketMatcher.
  EXPECT_CALL(*mock_local_client_, getBucket(preview_bucket_id_hash)).WillOnce(Return(nullptr));
  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(nullptr));
  EXPECT_CALL(*mock_local_client_,
              createBucket(ProtoEqIgnoreRepeatedFieldOrdering(preview_bucket_id),
                           preview_bucket_id_hash, ProtoEq(preview_expected_action),
                           testing::IsNull(), std::chrono::milliseconds::zero(), true))
      .WillOnce(Return());
  EXPECT_CALL(*mock_local_client_,
              createBucket(ProtoEqIgnoreRepeatedFieldOrdering(bucket_id), bucket_id_hash,
                           ProtoEq(expected_action), testing::IsNull(),
                           std::chrono::milliseconds::zero(), false))
      .WillOnce(Return());

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
}

TEST_F(FilterTest, DecodeHeaderWithPreviewTokenBucket) {
  addMatcherConfig(MatcherConfigType::ValidPreview);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({{"name", "prod"}});
  absl::flat_hash_map<std::string, std::string> expected_preview_bucket_ids(expected_bucket_ids);

  // The low priority config has a different bucket id intentionally.
  expected_preview_bucket_ids.insert({{"preview_name", "preview_test"}});
  // Expect request processing to check for both buckets, and create the missing actionable bucket.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  // The preview bucket will have a pre-cached TokenBucket for testing.
  BucketId preview_bucket_id = bucketIdFromMap(expected_preview_bucket_ids);
  size_t preview_bucket_id_hash = MessageUtil::hash(preview_bucket_id);

  // Expect the new actionable bucket to fallback to ALLOW_ALL.
  // The preview bucket's TokenBucket should log a DENY action, but the actionable bucket should
  // allow the traffic anyway.
  BucketAction expected_action;
  expected_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  *expected_action.mutable_bucket_id() = bucket_id;
  BucketAction preview_expected_action;
  preview_expected_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  *preview_expected_action.mutable_bucket_id() = preview_bucket_id;

  auto cached_preview_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  TokenBucket* preview_token_bucket = cached_preview_action->mutable_quota_assignment_action()
                                          ->mutable_rate_limit_strategy()
                                          ->mutable_token_bucket();
  preview_token_bucket->set_max_tokens(1);
  preview_token_bucket->mutable_tokens_per_fill()->set_value(1);
  preview_token_bucket->mutable_fill_interval()->set_seconds(60);
  std::shared_ptr<AtomicTokenBucketImpl> token_bucket_limiter =
      std::make_shared<AtomicTokenBucketImpl>(1, dispatcher_.timeSource(), 1 / 60);
  // All subsequent requests should deny for 60 (mock) seconds.
  EXPECT_TRUE(token_bucket_limiter->consume());

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> cached_preview_bucket = std::make_shared<CachedBucket>(
      preview_bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_preview_action), nullptr, std::chrono::milliseconds::zero(),
      no_assignment_action, token_bucket_limiter);
  // The actionable bucket has an allow-all no_assignment_action and no cached
  // assignment, so the traffic should be allowed regardless of the previewed TokenBucket.
  std::shared_ptr<CachedBucket> cached_allow_all_bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)), nullptr, nullptr,
      std::chrono::milliseconds::zero(), no_assignment_action, nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash))
      .WillOnce(Return(cached_allow_all_bucket));
  EXPECT_CALL(*mock_local_client_, getBucket(preview_bucket_id_hash))
      .WillOnce(Return(cached_preview_bucket));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(
      cached_preview_bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(cached_preview_bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed),
            1);
  EXPECT_EQ(
      cached_allow_all_bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed),
      2);
  EXPECT_EQ(
      cached_allow_all_bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 0);
}

TEST_F(FilterTest, UnsupportedRequestsPerTimeUnit) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // unsupported RequestsPerTimeUnit strategy.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  RateLimitStrategy::RequestsPerTimeUnit* requests_per_time_unit =
      cached_action->mutable_quota_assignment_action()
          ->mutable_rate_limit_strategy()
          ->mutable_requests_per_time_unit();
  requests_per_time_unit->set_requests_per_time_unit(10);
  requests_per_time_unit->set_time_unit(envoy::type::v3::RateLimitUnit::MINUTE);

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status;
  EXPECT_LOG_CONTAINS("warn", "RequestsPerTimeUnit is not yet supported by RLQS.",
                      { status = filter_->decodeHeaders(default_headers_, false); });

  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 2);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 0);
}

TEST_F(FilterTest, CachedBucketMissingStrategy) {
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();
  // Define the key value pairs that is used to build the bucket_id dynamically
  // via `custom_value` in the config.
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  verifyRequestMatchingSucceeded(expected_bucket_ids);

  // Expect request processing to check for an existing bucket, find one with an
  // unset strategy (e.g. if a user sets an invalid default bucket action & this
  // isn't caught in validation). Expect a fail-open.
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  *cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy() =
      RateLimitStrategy();

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  Http::FilterHeadersStatus status;
  EXPECT_LOG_CONTAINS("error",
                      "Bug: an RLQS bucket is cached with a missing quota_assignment_action or "
                      "rate_limit_strategy causing the filter to fail open.",
                      { status = filter_->decodeHeaders(default_headers_, false); });

  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 2);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 0);
}

// Tests for gRPC status functionality

TEST_F(FilterTest, DenyResponseWithExplicitGrpcStatus) {
  // Create a custom config with explicit grpc_status set to UNAVAILABLE
  const std::string config_yaml = R"EOF(
rlqs_server:
  google_grpc:
    target_uri: rate_limit_quota_server
    stat_prefix: rlqs
bucket_matchers:
  matcher_list:
    matchers:
    - predicate:
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
                "environment":
                  string_value: "staging"
            deny_response_settings:
              http_status:
                code: 429
              grpc_status:
                code: 14  # UNAVAILABLE
                message: "Service temporarily unavailable"
            expired_assignment_behavior:
              fallback_rate_limit:
                blanket_rule: ALLOW_ALL
            reporting_interval: 5s
)EOF";

  // Extract just the matcher portion for loading into xds::type::matcher::v3::Matcher
  const std::string matcher_yaml_explicit = R"EOF(
  matcher_list:
    matchers:
    - predicate:
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
                "environment":
                  string_value: "staging"
            deny_response_settings:
              http_status:
                code: 429
              grpc_status:
                code: 14
                message: "Service temporarily unavailable"
            expired_assignment_behavior:
              fallback_rate_limit:
                blanket_rule: ALLOW_ALL
            reporting_interval: 5s
  )EOF";

  FilterConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  xds::type::matcher::v3::Matcher matcher;
  TestUtility::loadFromYaml(matcher_yaml_explicit, matcher);
  addMatcherConfig(matcher);

  filter_config_ = std::make_shared<FilterConfig>(config);
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(filter_config_->rlqs_server());

  mock_local_client_ = new MockRateLimitClient();
  filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_,
                                                   absl::WrapUnique(mock_local_client_),
                                                   config_with_hash_key, match_tree_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Build headers that match the config
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"environment", "staging"}};

  // Set up bucket with DENY_ALL action
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids({{"environment", "staging"}});
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->set_blanket_rule(
      RateLimitStrategy::DENY_ALL);

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  // Expect sendLocalReply to be called with UNAVAILABLE gRPC status and custom message
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke(
          [](Http::Code, absl::string_view body_text, std::function<void(Http::ResponseHeaderMap&)>,
             const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view) {
            EXPECT_EQ(grpc_status, Grpc::Status::WellKnownGrpcStatus::Unavailable);
            EXPECT_EQ(body_text, "Service temporarily unavailable");
          }));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(headers, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 1);
}

TEST_F(FilterTest, DenyResponseDefaultBehavior) {
  // Test default behavior when grpc_status is not set
  addMatcherConfig(MatcherConfigType::Valid);
  createFilter();

  // Build headers that match the config
  absl::flat_hash_map<std::string, std::string> custom_value_pairs = {{"environment", "staging"},
                                                                      {"group", "envoy"}};
  buildCustomHeader(custom_value_pairs);

  // Set up bucket with DENY_ALL action
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids = custom_value_pairs;
  expected_bucket_ids.insert({"name", "prod"});
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->set_blanket_rule(
      RateLimitStrategy::DENY_ALL);

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  // Should use default behavior (absl::nullopt for gRPC status)
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                    absl::string_view) { EXPECT_EQ(grpc_status, absl::nullopt); }));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(default_headers_, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 1);
}

TEST_F(FilterTest, CustomGrpcMessageTest) {
  // Test that custom gRPC message is passed correctly in sendLocalReply body_text parameter
  const std::string config_yaml = R"EOF(
rlqs_server:
  google_grpc:
    target_uri: rate_limit_quota_server
    stat_prefix: rlqs
bucket_matchers:
  matcher_list:
    matchers:
    - predicate:
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
                "environment":
                  string_value: "staging"
            deny_response_settings:
              http_status:
                code: 429
              grpc_status:
                code: 8
                message: "Custom rate limit message from test"
            expired_assignment_behavior:
              fallback_rate_limit:
                blanket_rule: ALLOW_ALL
            reporting_interval: 5s
)EOF";

  // Extract just the matcher portion for loading into xds::type::matcher::v3::Matcher
  const std::string matcher_yaml = R"EOF(
matcher_list:
  matchers:
  - predicate:
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
              "environment":
                string_value: "staging"
          deny_response_settings:
            http_status:
              code: 429
            grpc_status:
              code: 8
              message: "Custom rate limit message from test"
          expired_assignment_behavior:
            fallback_rate_limit:
              blanket_rule: ALLOW_ALL
          reporting_interval: 5s
)EOF";

  FilterConfig config;
  TestUtility::loadFromYaml(config_yaml, config);

  xds::type::matcher::v3::Matcher matcher;
  TestUtility::loadFromYaml(matcher_yaml, matcher);
  addMatcherConfig(matcher);

  filter_config_ = std::make_shared<FilterConfig>(config);
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(filter_config_->rlqs_server());

  mock_local_client_ = new MockRateLimitClient();
  filter_ = std::make_unique<RateLimitQuotaFilter>(filter_config_, context_,
                                                   absl::WrapUnique(mock_local_client_),
                                                   config_with_hash_key, match_tree_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Build headers that match the config
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"environment", "staging"}};

  // Set up bucket with DENY_ALL action
  absl::flat_hash_map<std::string, std::string> expected_bucket_ids({{"environment", "staging"}});
  BucketId bucket_id = bucketIdFromMap(expected_bucket_ids);
  size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  auto cached_action = std::make_unique<RateLimitQuotaResponse::BucketAction>();
  cached_action->mutable_quota_assignment_action()->mutable_rate_limit_strategy()->set_blanket_rule(
      RateLimitStrategy::DENY_ALL);

  RateLimitQuotaResponse::BucketAction no_assignment_action;
  no_assignment_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

  std::shared_ptr<CachedBucket> bucket = std::make_shared<CachedBucket>(
      bucket_id, std::make_shared<QuotaUsage>(1, 0, std::chrono::nanoseconds(0)),
      std::move(cached_action), nullptr, std::chrono::milliseconds::zero(), no_assignment_action,
      nullptr);

  EXPECT_CALL(*mock_local_client_, getBucket(bucket_id_hash)).WillOnce(Return(bucket));

  // Expect sendLocalReply to be called with custom gRPC message in body_text parameter
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                          absl::string_view details) {
        // Check that the custom message is in the body_text parameter (for gRPC message)
        EXPECT_EQ(body, "Custom rate limit message from test");
        // Check that the gRPC status is RESOURCE_EXHAUSTED
        EXPECT_EQ(grpc_status, Grpc::Status::WellKnownGrpcStatus::ResourceExhausted);
        // Check that details contains our debug info
        EXPECT_EQ(details, "rate_limited_by_quota");
      }));

  Http::FilterHeadersStatus status = filter_->decodeHeaders(headers, false);
  EXPECT_EQ(status, Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(bucket->quota_usage->num_requests_allowed.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(bucket->quota_usage->num_requests_denied.load(std::memory_order_relaxed), 1);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
