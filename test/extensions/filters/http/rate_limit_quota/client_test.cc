#include "test/extensions/filters/http/rate_limit_quota/client_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

constexpr char SingleBukcetId[] = R"EOF(
  bucket:
    "fairshare_group_id":
      "mock_group"
)EOF";

class RateLimitClientTest : public testing::Test {
public:
  RateLimitTestClient test_client{};
};

using envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;

TEST_F(RateLimitClientTest, OpenAndCloseStream) {
  EXPECT_OK(test_client.client_->startStream(&test_client.stream_info_));
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

TEST_F(RateLimitClientTest, SendUsageReport) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  EXPECT_OK(test_client.client_->startStream(&test_client.stream_info_));
  bool end_stream = false;
  // Send quota usage report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, end_stream));
  const size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  test_client.client_->sendUsageReport(bucket_id_hash);
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

TEST_F(RateLimitClientTest, SendRequestAndReceiveResponse) {
  EXPECT_OK(test_client.client_->startStream(&test_client.stream_info_));
  ASSERT_NE(test_client.stream_callbacks_, nullptr);

  auto empty_request_headers = Http::RequestHeaderMapImpl::create();
  test_client.stream_callbacks_->onCreateInitialMetadata(*empty_request_headers);
  auto empty_response_headers = Http::ResponseHeaderMapImpl::create();
  test_client.stream_callbacks_->onReceiveInitialMetadata(std::move(empty_response_headers));

  // Send empty report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, false));
  test_client.client_->sendUsageReport(absl::nullopt);

  // `onQuotaResponse` callback is expected to be called.
  EXPECT_CALL(test_client.callbacks_, onQuotaResponse);
  RateLimitQuotaResponse resp;
  auto response_buf = Grpc::Common::serializeMessage(resp);
  EXPECT_TRUE(test_client.stream_callbacks_->onReceiveMessageRaw(std::move(response_buf)));

  auto empty_response_trailers = Http::ResponseTrailerMapImpl::create();
  test_client.stream_callbacks_->onReceiveTrailingMetadata(std::move(empty_response_trailers));

  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
  test_client.client_->onRemoteClose(0, "");
}

TEST_F(RateLimitClientTest, RestartStreamWhileInUse) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  EXPECT_OK(test_client.client_->startStream(&test_client.stream_info_));

  bool end_stream = false;
  // Send quota usage report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, end_stream));
  const size_t bucket_id_hash = MessageUtil::hash(bucket_id);
  test_client.client_->sendUsageReport(bucket_id_hash);
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();

  // Expect the stream to reopen while trying to send the next usage report.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, end_stream));
  test_client.client_->sendUsageReport(bucket_id_hash);
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();

  // Expect the client to handle a restart failure.
  EXPECT_CALL(*test_client.async_client_, startRaw(_, _, _, _)).WillOnce(testing::Return(nullptr));
  WAIT_FOR_LOG_CONTAINS("error", "Failed to start the stream to send reports.",
                        { test_client.client_->sendUsageReport(bucket_id_hash); });
}

TEST_F(RateLimitClientTest, HandlingDuplicateTokenBucketAssignments) {
  EXPECT_OK(test_client.client_->startStream(&test_client.stream_info_));
  ASSERT_NE(test_client.stream_callbacks_, nullptr);

  auto empty_request_headers = Http::RequestHeaderMapImpl::create();
  test_client.stream_callbacks_->onCreateInitialMetadata(*empty_request_headers);
  auto empty_response_headers = Http::ResponseHeaderMapImpl::create();
  test_client.stream_callbacks_->onReceiveInitialMetadata(std::move(empty_response_headers));

  // `onQuotaResponse` callback is expected to be called twice.
  EXPECT_CALL(test_client.callbacks_, onQuotaResponse).Times(3);

  ::envoy::type::v3::TokenBucket token_bucket;
  token_bucket.set_max_tokens(100);
  token_bucket.mutable_tokens_per_fill()->set_value(10);
  token_bucket.mutable_fill_interval()->set_seconds(1000);

  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  bucket_id.mutable_bucket()->insert({"fairshare_group_id", "mock_group"});
  const size_t bucket_id_hash = MessageUtil::hash(bucket_id);

  Bucket initial_bucket_state;
  initial_bucket_state.bucket_id = bucket_id;
  test_client.bucket_cache_.insert(
      {bucket_id_hash, std::make_unique<Bucket>(std::move(initial_bucket_state))});

  RateLimitQuotaResponse::BucketAction action;
  action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->mutable_token_bucket()
      ->MergeFrom(token_bucket);
  action.mutable_bucket_id()->MergeFrom(bucket_id);

  RateLimitQuotaResponse resp;
  resp.add_bucket_action()->MergeFrom(action);
  RateLimitQuotaResponse duplicate_resp;
  duplicate_resp.add_bucket_action()->MergeFrom(action);

  auto response_buf = Grpc::Common::serializeMessage(resp);
  auto duplicate_response_buf = Grpc::Common::serializeMessage(duplicate_resp);
  EXPECT_TRUE(test_client.stream_callbacks_->onReceiveMessageRaw(std::move(response_buf)));

  ASSERT_EQ(test_client.bucket_cache_.size(), 1);
  ASSERT_TRUE(test_client.bucket_cache_.contains(bucket_id_hash));
  Bucket* first_bucket = test_client.bucket_cache_.at(bucket_id_hash).get();
  TokenBucket* first_token_bucket_limiter = first_bucket->token_bucket_limiter.get();
  EXPECT_TRUE(first_token_bucket_limiter);

  // Send a duplicate response & expect the token bucket to be carried forward
  // in the cache to avoid resetting token consumption.
  EXPECT_TRUE(
      test_client.stream_callbacks_->onReceiveMessageRaw(std::move(duplicate_response_buf)));

  ASSERT_EQ(test_client.bucket_cache_.size(), 1);
  ASSERT_TRUE(test_client.bucket_cache_.contains(bucket_id_hash));
  Bucket* second_bucket = test_client.bucket_cache_.at(bucket_id_hash).get();
  TokenBucket* second_token_bucket_limiter = second_bucket->token_bucket_limiter.get();
  EXPECT_TRUE(second_token_bucket_limiter);
  EXPECT_EQ(first_token_bucket_limiter, second_token_bucket_limiter);

  // Expect the limiter to be replaced if the config changes.
  resp.mutable_bucket_action(0)
      ->mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->mutable_token_bucket()
      ->set_max_tokens(200);
  auto different_response_buf = Grpc::Common::serializeMessage(resp);
  EXPECT_TRUE(
      test_client.stream_callbacks_->onReceiveMessageRaw(std::move(different_response_buf)));

  ASSERT_EQ(test_client.bucket_cache_.size(), 1);
  ASSERT_TRUE(test_client.bucket_cache_.contains(bucket_id_hash));
  Bucket* third_bucket = test_client.bucket_cache_.at(bucket_id_hash).get();
  TokenBucket* third_token_bucket_limiter = third_bucket->token_bucket_limiter.get();
  EXPECT_TRUE(third_token_bucket_limiter);
  EXPECT_NE(first_token_bucket_limiter, third_token_bucket_limiter);

  auto empty_response_trailers = Http::ResponseTrailerMapImpl::create();
  test_client.stream_callbacks_->onReceiveTrailingMetadata(std::move(empty_response_trailers));

  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
  test_client.client_->onRemoteClose(0, "");
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
