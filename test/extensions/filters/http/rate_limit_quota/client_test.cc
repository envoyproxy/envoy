#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/common/http/header_map_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"

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
    // TODO(tyxia) Need to sync to head
    EXPECT_CALL(client_manager_, getOrCreateRawAsyncClient(_, _, _, _))
        .WillOnce(Invoke(this, &RateLimitStreamTest::doCreateAsyncClient));

    //client_ = std::make_unique<ExternalProcessorClientImpl>(client_manager_, stats_store_);
    client_ = createRateLimitClient(context_, grpc_service_);
  }

  Grpc::RawAsyncClientSharedPtr doCreateAsyncClient(Unused, Unused, Unused, Unused) {
    auto async_client = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client, startRaw("envoy.service.rate_limit_quota.v3.RateLimitQuotaService",
                                        "StreamRateLimitQuotas", _, _))
        .WillOnce(Invoke(this, &RateLimitStreamTest::doStartRaw));
    return async_client;
  }

  Grpc::RawAsyncStream* doStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                   const Http::AsyncClient::StreamOptions&) {
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  ~RateLimitStreamTest() override = default;

  envoy::config::core::v3::GrpcService grpc_service_;
  NiceMock<MockFactoryContext> context_;
  Grpc::MockAsyncClientManager client_manager_;
  Grpc::MockAsyncStream stream_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  // testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  RateLimitClientPtr client_;
};

TEST_F(RateLimitStreamTest, OpenCloseStream) {
  // client_->startStream();
  // auto stream = client_->startStream();
  // EXPECT_CALL(stream_, closeStream());
  // EXPECT_CALL(stream_, resetStream());
  // stream->close();
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
