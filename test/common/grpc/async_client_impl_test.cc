#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/async_client_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
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

    config.mutable_envoy_grpc()->set_cluster_name("test_cluster");

    auto& initial_metadata_entry = *config.mutable_initial_metadata()->Add();
    initial_metadata_entry.set_key("downstream-local-address");
    initial_metadata_entry.set_value("%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%");

    grpc_client_ = *AsyncClientImpl::create(cm_, config, test_time_.timeSystem());
    cm_.initializeThreadLocalClusters({"test_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(http_client_));
  }

  envoy::config::core::v3::GrpcService config;
  const Protobuf::MethodDescriptor* method_descriptor_;
  NiceMock<Http::MockAsyncClient> http_client_;
  NiceMock<Upstream::MockClusterManager> cm_;
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
  DangerousDeprecatedTestTime test_time_;
};

TEST_F(EnvoyAsyncClientImplTest, ThreadSafe) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;

  Thread::ThreadPtr thread = Thread::threadFactoryForTest().createThread([&]() {
    // Verify that using the grpc client in a different thread cause assertion failure.
    EXPECT_DEBUG_DEATH(grpc_client_->start(*method_descriptor_, grpc_callbacks,
                                           Http::AsyncClient::StreamOptions()),
                       "isThreadSafe");
  });
  thread->join();
}

// Validates that the host header is the cluster name in grpc config.
TEST_F(EnvoyAsyncClientImplTest, HostIsClusterNameByDefault) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));

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

// Validate that the HTTP details are reported in the gRPC error message.
TEST_F(EnvoyAsyncClientImplTest, HttpRcdReportedInGrpcErrorMessage) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(testing::Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  stream_info.setResponseCodeDetails("upstream_reset");

  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks,
              onRemoteClose(Status::WellKnownGrpcStatus::Internal, "upstream_reset"));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that the host header is the authority field in grpc config.
TEST_F(EnvoyAsyncClientImplTest, HostIsOverrideByConfig) {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");
  config.mutable_envoy_grpc()->set_authority("demo.com");

  grpc_client_ = *AsyncClientImpl::create(cm_, config, test_time_.timeSystem());
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
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

// Validates that "*-bin" client init metadata are based64 encoded.
TEST_F(EnvoyAsyncClientImplTest, BinaryMetadataInClientInitialMetadataIsBase64Escaped) {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");
  config.mutable_envoy_grpc()->set_authority("demo.com");

  auto initial_metadata_entry = config.mutable_initial_metadata()->Add();
  initial_metadata_entry->set_key("static-binary-metadata-bin");
  initial_metadata_entry->set_value("你好，世界。");

  initial_metadata_entry = config.mutable_initial_metadata()->Add();
  initial_metadata_entry->set_key("hello-world-in-japanese-bin");
  initial_metadata_entry->set_value("こんにちは 世界");

  grpc_client_ = *AsyncClientImpl::create(cm_, config, test_time_.timeSystem());
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  // Encoding is done after all initial-metadata insertion.
  EXPECT_CALL(grpc_callbacks,
              onCreateInitialMetadata(testing::Truly([](Http::RequestHeaderMap& headers) {
                headers.addCopy(Http::LowerCaseString("somemore-bin"), "更多bin");
                return true;
              })));
  EXPECT_CALL(
      http_stream,
      sendHeaders(
          testing::Truly([](Http::HeaderMap& headers) {
            EXPECT_EQ(headers.get(Http::LowerCaseString("static-binary-metadata-bin"))[0]
                          ->value()
                          .getStringView(),
                      "5L2g5aW977yM5LiW55WM44CC");
            EXPECT_EQ(headers.get(Http::LowerCaseString("hello-world-in-japanese-bin"))[0]
                          ->value()
                          .getStringView(),
                      "44GT44KT44Gr44Gh44GvIOS4lueVjA==");
            EXPECT_EQ(
                headers.get(Http::LowerCaseString("somemore-bin"))[0]->value().getStringView(),
                "5pu05aSaYmlu");
            return true;
          }),
          _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that "*-bin" server init metadata are NOT based64 decoded.
// See https://github.com/envoyproxy/envoy/issues/39054, we don't want arbitrary binary header
// values gets into Envoy before that's well understood by folks.
TEST_F(EnvoyAsyncClientImplTest, BinMetadataInServerInitialMetadataAreNotUnescaped) {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");
  config.mutable_envoy_grpc()->set_authority("demo.com");
  grpc_client_ = *AsyncClientImpl::create(cm_, config, test_time_.timeSystem());
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(grpc_callbacks, onReceiveInitialMetadata_(_))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers) {
        EXPECT_EQ(headers.get(Http::LowerCaseString("static-binary-metadata-bin"))[0]
                      ->value()
                      .getStringView(),
                  "5L2g5aW977yM5LiW55WM44CC");
        EXPECT_EQ(headers.get(Http::LowerCaseString("hello-world-in-japanese-bin"))[0]
                      ->value()
                      .getStringView(),
                  "44GT44KT44Gr44Gh44GvIOS4lueVjA==");
        EXPECT_EQ(headers.get(Http::LowerCaseString("somemore-bin"))[0]->value().getStringView(),
                  "5pu05aSaYmlu");
        return true;
      }));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) {
        http_callbacks->onHeaders(
            std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl{
                {"static-binary-metadata-bin", "5L2g5aW977yM5LiW55WM44CC"},
                {":status", "200"},
                {"hello-world-in-japanese-bin", "44GT44KT44Gr44Gh44GvIOS4lueVjA=="},
                {"somemore-bin", "5pu05aSaYmlu"}}),
            // This tells clients it's server initial metadata.
            /*end_stream=*/false);
        http_callbacks->onReset();
      }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that "*-bin" trailing metadata are based64 decoded.
// See https://github.com/envoyproxy/envoy/issues/39054, we don't want arbitrary binary header
// values gets into Envoy before that's well understood by folks.
TEST_F(EnvoyAsyncClientImplTest, BinMetadataInServerTrailinglMetadataAreNotUnescaped) {
  envoy::config::core::v3::GrpcService config;
  config.mutable_envoy_grpc()->set_cluster_name("test_cluster");
  config.mutable_envoy_grpc()->set_authority("demo.com");
  grpc_client_ = *AsyncClientImpl::create(cm_, config, test_time_.timeSystem());
  EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillRepeatedly(ReturnRef(http_client_));

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));
  EXPECT_CALL(http_stream, reset()); // onTrailers will trigger reset.
  EXPECT_CALL(grpc_callbacks, onReceiveTrailingMetadata_(_))
      .WillOnce(Invoke([&](const Http::ResponseTrailerMap& headers) {
        EXPECT_EQ(headers.get(Http::LowerCaseString("static-binary-metadata-bin"))[0]
                      ->value()
                      .getStringView(),
                  "5L2g5aW977yM5LiW55WM44CC");
        EXPECT_EQ(headers.get(Http::LowerCaseString("hello-world-in-japanese-bin"))[0]
                      ->value()
                      .getStringView(),
                  /*こんにちは 世界*/ "44GT44KT44Gr44Gh44GvIOS4lueVjA==");
        EXPECT_EQ(headers.get(Http::LowerCaseString("somemore-bin"))[0]->value().getStringView(),
                  /*更多bin*/ "5pu05aSaYmlu");
        return true;
      }));
  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) {
        http_callbacks->onHeaders(
            std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl{
                {"static-binary-metadata-bin", "5L2g5aW977yM5LiW55WM44CC"},
                {":status", "200"},
                {"hello-world-in-japanese-bin", "44GT44KT44Gr44Gh44GvIOS4lueVjA=="},
                {"somemore-bin", "5pu05aSaYmlu"}}),
            true);
        http_callbacks->onReset();
      }));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that the metadata header is the initial metadata in gRPC service config and the value
// is interpolated.
TEST_F(EnvoyAsyncClientImplTest, MetadataIsInitialized) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
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
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(expected_downstream_local_address), nullptr);
  StreamInfo::StreamInfoImpl parent_stream_info{test_time_.timeSystem(), connection_info_provider,
                                                StreamInfo::FilterState::LifeSpan::FilterChain};
  Http::AsyncClient::ParentContext parent_context{&parent_stream_info};

  Http::AsyncClient::StreamOptions stream_options;
  stream_options.setParentContext(parent_context);

  auto grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks, stream_options);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that metadata is initialized without async client parent context.
TEST_F(EnvoyAsyncClientImplTest, MetadataIsInitializedWithoutStreamInfo) {
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));

  EXPECT_CALL(http_stream, sendHeaders(_, _))
      .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset(); }));

  Tracing::MockSpan parent_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};

  EXPECT_CALL(parent_span, spawnChild_(_, "async helloworld.Greeter.SayHello egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("13")));
  EXPECT_CALL(*child_span, injectContext(_, _));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, setSampled(true));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));

  auto stream_options =
      Http::AsyncClient::StreamOptions().setParentSpan(parent_span).setSampled(true);

  auto grpc_stream = grpc_client_->start(*method_descriptor_, grpc_callbacks, stream_options);
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpStartFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, ""));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

// Validates that a failure in the HTTP client returns immediately with status
// UNAVAILABLE.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpStartFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(http_client_, start(_, _)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks, onFailure(Status::WellKnownGrpcStatus::Unavailable, "", _));
  helloworld::HelloRequest request_msg;

  Tracing::MockSpan active_span;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(active_span, spawnChild_(_, "async helloworld.Greeter.SayHello egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  EXPECT_CALL(*child_span, injectContext(_, _)).Times(0);

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Http::AsyncClient::RequestOptions());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validates that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, StreamHttpSendHeadersFail) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
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

// Validates that a failure to sendHeaders() in the HTTP client returns
// immediately with status INTERNAL.
TEST_F(EnvoyAsyncClientImplTest, RequestHttpSendHeadersFail) {
  MockAsyncRequestCallbacks<helloworld::HelloReply> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;
  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
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
  EXPECT_CALL(active_span, spawnChild_(_, "async helloworld.Greeter.SayHello egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("test_cluster")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("test_cluster")));
  EXPECT_CALL(*child_span, injectContext(_, _));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("13")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());

  auto* grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                          active_span, Http::AsyncClient::RequestOptions());
  EXPECT_EQ(grpc_request, nullptr);
}

// Validates that when the cluster is not present the grpc_client returns immediately with
// status UNAVAILABLE and error message "Cluster not available"
TEST_F(EnvoyAsyncClientImplTest, StreamHttpClientException) {
  MockAsyncStreamCallbacks<helloworld::HelloReply> grpc_callbacks;
  ON_CALL(cm_, getThreadLocalCluster(_)).WillByDefault(Return(nullptr));
  EXPECT_CALL(grpc_callbacks,
              onRemoteClose(Status::WellKnownGrpcStatus::Unavailable, "Cluster not available"));
  auto grpc_stream =
      grpc_client_->start(*method_descriptor_, grpc_callbacks, Http::AsyncClient::StreamOptions());
  EXPECT_EQ(grpc_stream, nullptr);
}

TEST_F(EnvoyAsyncClientImplTest, AsyncRequestDetach) {
  NiceMock<MockAsyncRequestCallbacks<helloworld::HelloReply>> grpc_callbacks;
  Http::AsyncClient::StreamCallbacks* http_callbacks;

  StreamInfo::StreamInfoImpl stream_info{test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain};
  NiceMock<Http::MockAsyncClientStream> http_stream;
  ON_CALL(Const(http_stream), streamInfo()).WillByDefault(ReturnRef(stream_info));
  ON_CALL(http_stream, streamInfo()).WillByDefault(ReturnRef(stream_info));

  EXPECT_CALL(http_client_, start(_, _))
      .WillOnce(
          Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
                                                 const Http::AsyncClient::StreamOptions&) {
            http_callbacks = &callbacks;
            return &http_stream;
          }));

  const std::string expected_downstream_local_address = "5.5.5.5";
  EXPECT_CALL(grpc_callbacks, onCreateInitialMetadata(_));
  EXPECT_CALL(http_stream, sendHeaders(_, _));

  // Prepare the parent context of this call.
  auto connection_info_provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
      std::make_shared<Network::Address::Ipv4Instance>(expected_downstream_local_address), nullptr);

  StreamInfo::StreamInfoImpl parent_stream_info{test_time_.timeSystem(), connection_info_provider,
                                                StreamInfo::FilterState::LifeSpan::FilterChain};
  Http::AsyncClient::ParentContext parent_context{&parent_stream_info};
  testing::NiceMock<Http::MockSidestreamWatermarkCallbacks> watermark_callbacks;
  auto parent_span = std::make_unique<Tracing::NullSpan>();

  Http::AsyncClient::StreamOptions stream_options;
  stream_options.setParentContext(parent_context);
  stream_options.setSidestreamWatermarkCallbacks(&watermark_callbacks);
  stream_options.setParentSpan(*parent_span);

  helloworld::HelloRequest request_msg;
  auto grpc_request = grpc_client_->send(*method_descriptor_, request_msg, grpc_callbacks,
                                         *parent_span, stream_options);
  EXPECT_NE(grpc_request, nullptr);

  EXPECT_CALL(http_stream, removeWatermarkCallbacks());
  stream_info.setParentStreamInfo(parent_stream_info); // Mock Envoy setting parent stream info.

  grpc_request->detach();

  EXPECT_FALSE(grpc_request->streamInfo().parentStreamInfo().has_value());

  // Clean up by simulating a reset from the HTTP stream.
  http_callbacks->onReset();
}

} // namespace
} // namespace Grpc
} // namespace Envoy
