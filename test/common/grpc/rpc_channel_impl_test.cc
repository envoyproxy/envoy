#include <chrono>
#include <cstdint>
#include <string>

#include "common/grpc/common.h"
#include "common/grpc/rpc_channel_impl.h"
#include "common/http/message_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef BAZEL_BRINGUP
#include "test/proto/helloworld.pb.h"
#else
#include "test/generated/helloworld.pb.h"
#endif
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Grpc {

class GrpcRequestImplTest : public testing::Test {
public:
  GrpcRequestImplTest() : http_async_client_request_(&cm_.async_client_) {
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  }

  void expectNormalRequest(
      const Optional<std::chrono::milliseconds> timeout = Optional<std::chrono::milliseconds>()) {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
        .WillOnce(Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                             Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
          http_request_ = std::move(request);
          http_callbacks_ = &callbacks;
          return &http_async_client_request_;
        }));
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  MockRpcChannelCallbacks grpc_callbacks_;
  RpcChannelImpl grpc_request_{cm_, "fake_cluster", grpc_callbacks_,
                               Optional<std::chrono::milliseconds>()};
  helloworld::Greeter::Stub service_{&grpc_request_};
  Http::MockAsyncClientRequest http_async_client_request_;
  Http::MessagePtr http_request_;
  Http::AsyncClient::Callbacks* http_callbacks_{};
};

TEST_F(GrpcRequestImplTest, NoError) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  Http::LowerCaseString header_key("foo");
  std::string header_value("bar");
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_))
      .WillOnce(Invoke([&](Http::HeaderMap& headers)
                           -> void { headers.addStatic(header_key, header_value); }));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::TestHeaderMapImpl expected_request_headers{{":method", "POST"},
                                                   {":path", "/helloworld.Greeter/SayHello"},
                                                   {":authority", "fake_cluster"},
                                                   {"content-type", "application/grpc"},
                                                   {"foo", "bar"}};

  EXPECT_THAT(http_request_->headers(), HeaderMapEqualRef(&expected_request_headers));

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  helloworld::HelloReply inner_response;
  inner_response.set_message("hello a name");
  response_http_message->body() = Common::serializeBody(inner_response);
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onSuccess());
  http_callbacks_->onSuccess(std::move(response_http_message));
  EXPECT_EQ(response.SerializeAsString(), inner_response.SerializeAsString());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.helloworld.Greeter.SayHello.success")
                     .value());
}

TEST_F(GrpcRequestImplTest, Non200Response) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "non-200 response code"));
  http_callbacks_->onSuccess(std::move(response_http_message));
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc.helloworld.Greeter.SayHello.failure")
                     .value());
}

TEST_F(GrpcRequestImplTest, NoResponseTrailers) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "no response trailers"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadGrpcStatusInHeaderOnlyResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}, {"grpc-status", "foo"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad grpc-status header"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, HeaderOnlyFailure) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(
      new Http::ResponseMessageImpl(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "3"}, {"grpc-message", "hello"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(3), "hello"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadGrpcStatusInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", ""}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad grpc-status trailer"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, GrpcStatusNonZeroInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_http_message->trailers(Http::HeaderMapPtr{
      new Http::TestHeaderMapImpl{{"grpc-status", "1"}, {"grpc-message", "hello"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(1), "hello"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, ShortBodyInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_http_message->body().reset(new Buffer::OwnedImpl("aaa"));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad serialized body"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadMessageInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_http_message->body().reset(new Buffer::OwnedImpl("aaaaaaaa"));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad serialized body"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, HttpAsyncRequestFailure) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "stream reset"));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
}

TEST_F(GrpcRequestImplTest, NoHttpAsyncRequest) {
  EXPECT_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));
  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "non-200 response code"));

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);
}

TEST_F(GrpcRequestImplTest, Cancel) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  EXPECT_CALL(http_async_client_request_, cancel());
  grpc_request_.cancel();
}

TEST_F(GrpcRequestImplTest, RequestTimeoutSet) {
  const Optional<std::chrono::milliseconds> timeout(std::chrono::milliseconds(100));
  RpcChannelImpl grpc_request_timeout{cm_, "fake_cluster", grpc_callbacks_, timeout};
  helloworld::Greeter::Stub service_timeout{&grpc_request_timeout};
  expectNormalRequest(timeout);
  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_timeout.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  helloworld::HelloReply inner_response;
  inner_response.set_message("hello a name");

  response_http_message->body() = Common::serializeBody(inner_response);
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onSuccess());
  http_callbacks_->onSuccess(std::move(response_http_message));
}

} // Grpc
