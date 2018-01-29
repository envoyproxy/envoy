#include "common/grpc/async_client_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Grpc {
namespace {

class EnvoyAsyncClientImplTest : public testing::Test {
public:
  EnvoyAsyncClientImplTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        grpc_client_(new AsyncClientImpl(cm_, "test_cluster")) {
    ON_CALL(cm_, httpAsyncClientForCluster("test_cluster")).WillByDefault(ReturnRef(http_client_));
  }

  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::unique_ptr<AsyncClientImpl> grpc_client_;
};

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpStartFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _, false)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Unavailable, ""));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpStartFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _, true)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::GrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "14"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_)).Times(0);

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpSendHeadersFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _, false))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Optional<std::chrono::milliseconds>&, bool) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap& headers, bool end_stream) {
        UNREFERENCED_PARAMETER(headers);
        UNREFERENCED_PARAMETER(end_stream);
        http_callbacks->onReset();
      }));
  EXPECT_CALL(grpc_callbacks, onReceiveTrailingMetadata_(_));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::GrpcStatus::Internal, ""));
  auto* grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpSendHeadersFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _, true))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Optional<std::chrono::milliseconds>&, bool) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap& headers, bool end_stream) {
        UNREFERENCED_PARAMETER(headers);
        UNREFERENCED_PARAMETER(end_stream);
        http_callbacks->onReset();
      }));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::GrpcStatus::Internal, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, "test_cluster"));
  EXPECT_CALL(*child_span, injectContext(_));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "13"));
  EXPECT_CALL(*child_span, setTag(Tracing::Tags::get().ERROR, Tracing::Tags::get().TRUE));
  EXPECT_CALL(*child_span, finishSpan());

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Optional<std::chrono::milliseconds>());
  EXPECT_EQ(grpc_request, nullptr);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
