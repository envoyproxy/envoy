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
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse resp;
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

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
