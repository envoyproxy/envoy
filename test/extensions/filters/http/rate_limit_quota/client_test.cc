// #include "envoy/config/core/v3/grpc_service.pb.h"

// #include "source/common/grpc/common.h"
// #include "source/common/http/header_map_impl.h"
// #include "source/extensions/filters/http/rate_limit_quota/client.h"
// #include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

// #include "test/extensions/filters/http/rate_limit_quota/mocks.h"
// #include "test/mocks/grpc/mocks.h"
// #include "test/mocks/server/mocks.h"
// #include "test/test_common/status_utility.h"

// #include "gmock/gmock.h"
// #include "gtest/gtest.h"

#include "test/extensions/filters/http/rate_limit_quota/client_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Server::Configuration::MockFactoryContext;
using testing::Invoke;
using testing::Unused;

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

struct RateLimitStreamTest : public testing::Test {
  RateLimitTestClient test_client{};
};

TEST_F(RateLimitStreamTest, OpenAndCloseStream) {
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

TEST_F(RateLimitStreamTest, SendUsageReport) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  bool end_stream = true;
  // Send quota usage report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, end_stream));
  test_client.client_->sendUsageReport("cloud_12345_67890_td_rlqs", bucket_id);
  EXPECT_CALL(test_client.stream_, closeStream());
  EXPECT_CALL(test_client.stream_, resetStream());
  test_client.client_->closeStream();
}

// TODO(tyxia) Test more in this sub-test!!!
TEST_F(RateLimitStreamTest, SendRequestAndReceiveResponse) {
  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  ASSERT_NE(test_client.stream_callbacks_, nullptr);

  auto empty_request_headers = Http::RequestHeaderMapImpl::create();
  test_client.stream_callbacks_->onCreateInitialMetadata(*empty_request_headers);
  auto empty_response_headers = Http::ResponseHeaderMapImpl::create();
  test_client.stream_callbacks_->onReceiveInitialMetadata(std::move(empty_response_headers));

  // Send empty report and ensure that we get it.
  EXPECT_CALL(test_client.stream_, sendMessageRaw_(_, true));
  test_client.client_->sendUsageReport("cloud_12345_67890_td_rlqs", absl::nullopt);

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

// TODO(tyxia) Update allowed/denied check
TEST_F(RateLimitStreamTest, BuildUsageReport) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  std::string domain = "cloud_12345_67890_td_rlqs";

  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  RateLimitQuotaUsageReports report = test_client.client_->buildUsageReport(domain, bucket_id);
  EXPECT_EQ(report.domain(), domain);
  EXPECT_EQ(report.bucket_quota_usages().size(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);
}

TEST_F(RateLimitStreamTest, BuildMultipleReports) {
  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id;
  TestUtility::loadFromYaml(SingleBukcetId, bucket_id);
  std::string domain = "cloud_12345_67890_td_rlqs";

  EXPECT_OK(test_client.client_->startStream(test_client.stream_info_));
  // Build the usage report with 2 entries with same domain and bucket id.
  RateLimitQuotaUsageReports report;
  for (int i = 0; i < 2; ++i) {
    report = test_client.client_->buildUsageReport(domain, bucket_id);
  }

  EXPECT_EQ(report.domain(), domain);
  EXPECT_EQ(report.bucket_quota_usages().size(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 2);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);

  ::envoy::service::rate_limit_quota::v3::BucketId bucket_id2;
  TestUtility::loadFromYaml(MultipleBukcetId, bucket_id2);
  // Build the usage report with the entry with different bucket id which will create a new entry in
  // report.
  report = test_client.client_->buildUsageReport(domain, bucket_id2);
  EXPECT_EQ(report.bucket_quota_usages().size(), 2);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 2);
  EXPECT_EQ(report.bucket_quota_usages(1).num_requests_allowed(), 1);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);

  // Update the usage report with old bucket id.
  report = test_client.client_->buildUsageReport(domain, bucket_id);
  EXPECT_EQ(report.bucket_quota_usages().size(), 2);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_allowed(), 3);
  EXPECT_EQ(report.bucket_quota_usages(0).num_requests_denied(), 0);
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
