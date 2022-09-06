#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Server::Configuration::MockFactoryContext;

using testing::Invoke;
using testing::Unused;

class RateLimitStreamTest : public testing::Test {
public:
  void SetUp() override {
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("rate_limit_quota");
    // Set the expected behavior for async_client_manager in mock context.
    // Note, we need to set it through `MockFactoryContext` rather than `MockAsyncClientManager`
    // directly because the rate limit client object below requires context argument as the input.
    EXPECT_CALL(context_.cluster_manager_.async_client_manager_, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce(Invoke(this, &RateLimitStreamTest::mockCreateAsyncClient));

    client_ = createRateLimitClient(context_, grpc_service_);
  }

  Grpc::RawAsyncClientSharedPtr mockCreateAsyncClient(Unused, Unused, Unused) {
    auto async_client = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client, startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                                        "StreamRateLimitQuotas", _, _))
        .WillOnce(Invoke(this, &RateLimitStreamTest::mockStartRaw));

    return async_client;
  }

  Grpc::RawAsyncStream* mockStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                     const Http::AsyncClient::StreamOptions&) {
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  ~RateLimitStreamTest() override = default;

  NiceMock<MockFactoryContext> context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::MockAsyncStream stream_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  RateLimitClientPtr client_;

  bool grpc_closed_ = false;
};

TEST_F(RateLimitStreamTest, OpenAndCloseStream) {
  EXPECT_OK(client_->startStream(stream_info_));
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  client_->closeStream();
}

TEST_F(RateLimitStreamTest, SendRequest) {
  EXPECT_OK(client_->startStream(stream_info_));
  // Send empty report and ensure that we get it.
  EXPECT_CALL(stream_, sendMessageRaw_(_, /*end_stream=*/false));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports report;
  client_->send(std::move(report), /*end_stream=*/false);
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  client_->closeStream();
}

TEST_F(RateLimitStreamTest, CloseOnRemote) {
  EXPECT_OK(client_->startStream(stream_info_));
  // Send empty report and ensure that we get it.
  EXPECT_CALL(stream_, sendMessageRaw_(_, /*end_stream=*/true));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports report;
  client_->send(std::move(report), /*end_stream=*/true);
  // EXPECT_CALL(stream_, closeStream()).Times(0);
  // EXPECT_CALL(stream_, resetStream()).Times(0);
  // client_->closeStream();
}

// TEST_F(RateLimitStreamTest, StreamClosed) {
//   EXPECT_OK(client_->startStream());
//   ASSERT_NE(stream_callbacks_, nullptr);
//   EXPECT_FALSE(grpc_closed_);
//   EXPECT_EQ(grpc_status_, Grpc::Status::WellKnownGrpcStatus::Ok);
//   stream_callbacks_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "");
//   EXPECT_TRUE(grpc_closed_);
//   EXPECT_EQ(grpc_status_, Grpc::Status::WellKnownGrpcStatus::Ok);
//   client_->closeStream();
// }

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
