#include "envoy/config/core/v3/grpc_service.pb.h"

#include "common/grpc/async_client_impl.h"
#include "common/network/address_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {
namespace {

class EnvoyAsyncClientImplTest : public testing::Test {
public:
  EnvoyAsyncClientImplTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {
    envoy::config::core::v3::GrpcService config;
    config.mutable_envoy_grpc()->set_cluster_name("test_cluster");

    auto& initial_metadata_entry = *config.mutable_initial_metadata()->Add();
    initial_metadata_entry.set_key("downstream-local-address");
    initial_metadata_entry.set_value("%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%");

    grpc_client_ = std::make_unique<AsyncClientImpl>(cm_, config, test_time_.timeSystem());
    ON_CALL(cm_, httpAsyncClientForCluster("test_cluster")).WillByDefault(ReturnRef(http_client_));
  }

  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
  DangerousDeprecatedTestTime test_time_;
};

// Validate that the host header is the cluster name in grpc config.
TEST_F(EnvoyAsyncClientImplTest, HostIsClusterNameByDefault) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));

  EXPECT_CALL(grpc_callbacks,
              onCreateInitialMetadata(testing::Truly([](Http::RequestHeaderMap& headers) {
                return headers.Host()->value() == "test_cluster";
              })));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that the host header is the authority field in grpc config.
TEST_F(EnvoyAsyncClientImplTest, HostIsOverrideByConfig) {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");
  config.mutable_envoy_grpc()->set_authority("demo.com");

  grpc_client_ = std::make_unique<AsyncClientImpl>(cm_, config, test_time_.timeSystem());
  EXPECT_CALL(cm_, httpAsyncClientForCluster("test_cluster"))
      .WillRepeatedly(ReturnRef(http_client_));

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));

  EXPECT_CALL(grpc_callbacks,
              onCreateInitialMetadata(testing::Truly([](Http::RequestHeaderMap& headers) {
                return headers.Host()->value() == "demo.com";
              })));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that the metadata header is the initial metadata in gRPC service config and the value is
// interpolated.
TEST_F(EnvoyAsyncClientImplTest, MetadataIsInitialized) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));

  const std::string expected_downstream_local_address = "5.5.5.5";
  EXPECT_CALL(grpc_callbacks,
              onCreateInitialMetadata(testing::Truly([&expected_downstream_local_address](
                                                         Http::RequestHeaderMap& headers) {
                return headers.get(Http::LowerCaseString("downstream-local-address"))[0]->value() ==
                       expected_downstream_local_address;
              })));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));

  // Prepare the parent context of this call.
  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem()};
  stream_info.setDownstreamLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>(expected_downstream_local_address));
  Http::AsyncClient::ParentContext parent_context{&stream_info};

  Http::AsyncClient::StreamOptions stream_options;
  stream_options.setParentContext(parent_context);

  auto grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks, stream_options);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpStartFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, ""));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpStartFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::WellKnownGrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_)).Times(0);

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Http::AsyncClient::RequestOptions());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpSendHeadersFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
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
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::WellKnownGrpcStatus::Internal, ""));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validate that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpSendHeadersFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
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
  EXPECT_CALL(grpc_callbacks, onFailure(Status::WellKnownGrpcStatus::Internal, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async test_cluster egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, injectContext(_));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("13")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Http::AsyncClient::RequestOptions());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validate that when the cluster is not present the grpc_client returns immediately with
// status UNAVAILABLE and error message "Cluster not available"
TEST_F(EnvoyAsyncClientImplTest, StreamHttpClientException) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks,
              onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, "Cluster not available"));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
