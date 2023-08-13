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

constexpr char MultipleBukcetId[] = R"EOF(
  bucket:
    "fairshare_group_id":
      "mock_group"
    "fairshare_project_id":
      "mock_project"
    "fairshare_user_id":
      "test"
)EOF";

class RateLimitClientTest : public testing::Test {
public:
  RateLimitTestClient test_client{};
};

TEST_F(RateLimitClientTest, OpenAndCloseStream) {
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

TEST_F(RateLimitClientTest, SendUsageReport) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  bool end_stream = false;
  // Send quota usage report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, end_stream));
  test_client.client_->sendUsageReport(bucket_id);
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

TEST_F(RateLimitClientTest, SendRequestAndReceiveResponse) {
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
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

TEST_F(RateLimitClientTest, BuildUsageReport) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);

  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  RateLimitQuotaUsageReports report = test_client.client_->buildUsageReport(bucket_id);
  EXPECT_EQ(report.domain(), test_client.domain_);
  EXPECT_EQ(report.bucket_quota_usages().size(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);
}

TEST_F(RateLimitClientTest, BuildMultipleReports) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);

  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  // Build the usage report with 2 entries with same domain and bucket id.
  RateLimitQuotaUsageReports report;
  for (int i = 0; i < 2; ++i) {
    report = test_client.client_->buildUsageReport(bucket_id);
  }

  EXPECT_EQ(report.domain(), test_client.domain_);
  EXPECT_EQ(report.bucket_quota_usages().size(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 2);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);

  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id2;
  TestUtility::loadFromYaml(MultipleBukcetId, bucket_id2);
  // Build the usage report with the entry with different bucket id which will create a new entry in
  // report.
  report = test_client.client_->buildUsageReport(bucket_id2);
  EXPECT_EQ(report.bucket_quota_usages().size(), 2);
  for (auto usage : report.bucket_quota_usages()) {
    if (Protobuf::util::MessageDifferencer::Equals(usage.bucket_id(), bucket_id)) {
      EXPECT_EQ(usage.num_requests_allowed(), 2);
    } else {
      EXPECT_EQ(usage.num_requests_allowed(), 1);
    }
    EXPECT_EQ(usage.num_requests_denied(), 0);
  }

  // Update the usage report with old bucket id.
  report = test_client.client_->buildUsageReport(bucket_id);
  EXPECT_EQ(report.bucket_quota_usages().size(), 2);
  for (auto usage : report.bucket_quota_usages()) {
    if (Protobuf::util::MessageDifferencer::Equals(usage.bucket_id(), bucket_id)) {
      EXPECT_EQ(usage.num_requests_allowed(), 3);
    }
    EXPECT_EQ(usage.num_requests_denied(), 0);
  }
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
