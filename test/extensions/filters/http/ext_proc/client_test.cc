#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

using testing::Invoke;
using testing::Unused;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

class ExtProcStreamTest : public testing::Test, public ExternalProcessorCallbacks {
public:
  ~ExtProcStreamTest() override = default;

protected:
  void SetUp() override {
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("test");

    EXPECT_CALL(client_manager_, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce(Invoke(this, &ExtProcStreamTest::doFactory));

    client_ = std::make_unique<ExternalProcessorClientImpl>(client_manager_, stats_store_);
  }

  Grpc::RawAsyncClientSharedPtr doFactory(Unused, Unused, Unused) {
    auto async_client = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client,
                startRaw("envoy.service.ext_proc.v3.ExternalProcessor", "Process", _, _))
        .WillOnce(Invoke(this, &ExtProcStreamTest::doStartRaw));
    return async_client;
  }

  Grpc::RawAsyncStream* doStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                   const Http::AsyncClient::StreamOptions&) {
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  // ExternalProcessorCallbacks
  void onReceiveMessage(std::unique_ptr<ProcessingResponse>&& response) override {
    last_response_ = std::move(response);
  }

  void onGrpcError(Grpc::Status::GrpcStatus status) override { grpc_status_ = status; }

  void onGrpcClose() override { grpc_closed_ = true; }

  std::unique_ptr<ProcessingResponse> last_response_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  bool grpc_closed_ = false;

  envoy::config::core::v3::GrpcService grpc_service_;
  ExternalProcessorClientPtr client_;
  Grpc::MockAsyncClientManager client_manager_;
  Grpc::MockAsyncStream stream_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  testing::NiceMock<Stats::MockStore> stats_store_;
};

TEST_F(ExtProcStreamTest, OpenCloseStream) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, SendToStream) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  // Send something and ensure that we get it. Doesn't really matter what.
  EXPECT_CALL(stream_, sendMessageRaw_(_, false));
  ProcessingRequest req;
  stream->send(std::move(req), false);
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, SendAndClose) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  EXPECT_CALL(stream_, sendMessageRaw_(_, true));
  ProcessingRequest req;
  stream->send(std::move(req), true);
}

TEST_F(ExtProcStreamTest, ReceiveFromStream) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  ASSERT_NE(stream_callbacks_, nullptr);
  // Send something and ensure that we get it. Doesn't really matter what.
  ProcessingResponse resp;

  // These do nothing at the moment
  auto empty_request_headers = Http::RequestHeaderMapImpl::create();
  stream_callbacks_->onCreateInitialMetadata(*empty_request_headers);

  auto empty_response_headers = Http::ResponseHeaderMapImpl::create();
  stream_callbacks_->onReceiveInitialMetadata(std::move(empty_response_headers));

  auto response_buf = Grpc::Common::serializeMessage(resp);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  EXPECT_TRUE(stream_callbacks_->onReceiveMessageRaw(std::move(response_buf)));
  EXPECT_TRUE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);

  auto empty_response_trailers = Http::ResponseTrailerMapImpl::create();
  stream_callbacks_->onReceiveTrailingMetadata(std::move(empty_response_trailers));

  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, StreamClosed) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  ASSERT_NE(stream_callbacks_, nullptr);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  stream_callbacks_->onRemoteClose(0, "");
  EXPECT_FALSE(last_response_);
  EXPECT_TRUE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  stream->close();
}

TEST_F(ExtProcStreamTest, StreamError) {
  auto stream = client_->start(*this, grpc_service_, stream_info_);
  ASSERT_NE(stream_callbacks_, nullptr);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  stream_callbacks_->onRemoteClose(123, "Some sort of gRPC error");
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 123);
  stream->close();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
