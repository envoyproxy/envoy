#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "source/common/grpc/google_async_client_impl.h"
#endif

#include "source/common/http/async_client_impl.h"
#include "source/common/http/codes.h"
#include "source/common/http/http2/conn_pool.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/stats/symbol_table.h"

#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/grpc/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/global.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::AtLeast;
using testing::AtMost;
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::IsEmpty;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {
namespace {

const char HELLO_REQUEST[] = "ABC";
const char HELLO_REPLY[] = "DEFG";

MATCHER_P(HelloworldReplyEq, rhs, "") { return arg.message() == rhs; }

using TestMetadata = std::vector<std::pair<Http::LowerCaseString, std::string>>;

// Use in EXPECT_CALL(foo, bar(_)).WillExitIfNeeded() to exit dispatcher loop if
// there are no longer any pending events in DispatcherHelper.
#define WillExitIfNeeded()                                                                         \
  WillOnce(InvokeWithoutArgs([this] { dispatcher_helper_.exitDispatcherIfNeeded(); }))

// Utility to assist with keeping track of pending gmock expected events when
// deferring execution to the dispatcher. The dispatcher can be run using this
// helper until all pending events are completed.
class DispatcherHelper {
public:
  DispatcherHelper(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  void exitDispatcherIfNeeded() {
    ENVOY_LOG_MISC(debug, "Checking exit with {} events pending", pending_stream_events_);
    ASSERT(pending_stream_events_ > 0);
    if (--pending_stream_events_ == 0) {
      dispatcher_.exit();
    }
  }

  void runDispatcher() {
    ENVOY_LOG_MISC(debug, "Run dispatcher with {} events pending", pending_stream_events_);
    if (pending_stream_events_ > 0) {
      dispatcher_.run(Event::Dispatcher::RunType::Block);
    }
  }

  void setStreamEventPending() {
    ++pending_stream_events_;
    ENVOY_LOG_MISC(debug, "Set event pending, now {} events pending", pending_stream_events_);
  }

  uint32_t pending_stream_events_{};
  Event::Dispatcher& dispatcher_;
};

struct RequestArgs {
  helloworld::HelloRequest* request = nullptr;
  bool end_stream = false;
};

// Stream related test utilities.
class HelloworldStream : public MockAsyncStreamCallbacks<helloworld::HelloReply> {
public:
  HelloworldStream(DispatcherHelper& dispatcher_helper) : dispatcher_helper_(dispatcher_helper) {}

  void sendRequest(RequestArgs request_args = {}) {
    helloworld::HelloRequest request_msg;
    request_msg.set_name(HELLO_REQUEST);
    // Update the request pointer to local request message when it is not set at caller site (i.e.,
    // nullptr).
    if (request_args.request == nullptr) {
      request_args.request = &request_msg;
    }

    grpc_stream_->sendMessage(*request_args.request, request_args.end_stream);

    helloworld::HelloRequest received_msg;
    AssertionResult result =
        fake_stream_->waitForGrpcMessage(dispatcher_helper_.dispatcher_, received_msg);
    RELEASE_ASSERT(result, result.message());
    EXPECT_THAT(*request_args.request, ProtoEq(received_msg));
  }

  void expectInitialMetadata(const TestMetadata& metadata) {
    EXPECT_CALL(*this, onReceiveInitialMetadata_(_))
        .WillOnce(Invoke([this, &metadata](const Http::HeaderMap& received_headers) {
          Http::TestResponseHeaderMapImpl stream_headers(received_headers);
          for (const auto& value : metadata) {
            EXPECT_EQ(value.second, stream_headers.get_(value.first));
          }
          dispatcher_helper_.exitDispatcherIfNeeded();
        }));
    dispatcher_helper_.setStreamEventPending();
  }

  void expectTrailingMetadata(const TestMetadata& metadata) {
    EXPECT_CALL(*this, onReceiveTrailingMetadata_(_))
        .WillOnce(Invoke([this, &metadata](const Http::HeaderMap& received_headers) {
          Http::TestResponseTrailerMapImpl stream_headers(received_headers);
          for (auto& value : metadata) {
            EXPECT_EQ(value.second, stream_headers.get_(value.first));
          }
          dispatcher_helper_.exitDispatcherIfNeeded();
        }));
    dispatcher_helper_.setStreamEventPending();
  }

  void sendServerInitialMetadata(const TestMetadata& metadata) {
    Http::HeaderMapPtr reply_headers{new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
    for (auto& value : metadata) {
      reply_headers->addReference(value.first, value.second);
    }
    expectInitialMetadata(metadata);
    fake_stream_->startGrpcStream(false);
    fake_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl(*reply_headers), false);
  }

  // `check_response_size` only could be set to true in Envoy gRPC because bytes metering is only
  // implemented in Envoy gPRC mode.
  void sendReply(bool check_response_size = false) {
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*this, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY)))
        .WillOnce(Invoke([this, check_response_size](const helloworld::HelloReply& reply_msg) {
          if (check_response_size) {
            auto recv_buf = Common::serializeMessage(reply_msg);
            // gRPC message in Envoy gRPC mode is prepended with a frame header.
            Common::prependGrpcFrameHeader(*recv_buf);
            // Verify that the number of received byte that is tracked in the stream info equals to
            // the length of reply response buffer.
            auto upstream_meter = this->grpc_stream_->streamInfo().getUpstreamBytesMeter();
            uint64_t total_bytes_rev = upstream_meter->wireBytesReceived();
            uint64_t header_bytes_rev = upstream_meter->headerBytesReceived();
            // In HTTP2 codec, H2_FRAME_HEADER_SIZE is always included in bytes meter so we need to
            // account for it in the check here as well.
            EXPECT_EQ(total_bytes_rev - header_bytes_rev,
                      recv_buf->length() + Http::Http2::H2_FRAME_HEADER_SIZE);
          }
          dispatcher_helper_.exitDispatcherIfNeeded();
        }));
    dispatcher_helper_.setStreamEventPending();
    fake_stream_->sendGrpcMessage<helloworld::HelloReply>(reply);
  }

  void expectGrpcStatus(Status::GrpcStatus grpc_status) {
    if (grpc_status == Status::WellKnownGrpcStatus::InvalidCode) {
      EXPECT_CALL(*this, onRemoteClose(_, _)).WillExitIfNeeded();
    } else if (grpc_status > Status::WellKnownGrpcStatus::MaximumKnown) {
      EXPECT_CALL(*this, onRemoteClose(Status::WellKnownGrpcStatus::InvalidCode, _))
          .WillExitIfNeeded();
    } else {
      EXPECT_CALL(*this, onRemoteClose(grpc_status, _)).WillExitIfNeeded();
    }
    dispatcher_helper_.setStreamEventPending();
  }

  void sendServerTrailers(Status::GrpcStatus grpc_status, const std::string& grpc_message,
                          const TestMetadata& metadata, bool trailers_only = false) {
    Http::TestResponseTrailerMapImpl reply_trailers{
        {"grpc-status", std::to_string(enumToInt(grpc_status))}};
    if (!grpc_message.empty()) {
      reply_trailers.addCopy("grpc-message", grpc_message);
    }
    if (trailers_only) {
      reply_trailers.addCopy(":status", "200");
    }
    for (const auto& value : metadata) {
      reply_trailers.addCopy(value.first, value.second);
    }
    if (trailers_only) {
      expectInitialMetadata(empty_metadata_);
    }
    expectTrailingMetadata(metadata);
    expectGrpcStatus(grpc_status);
    if (trailers_only) {
      fake_stream_->encodeHeaders(reply_trailers, true);
    } else {
      fake_stream_->encodeTrailers(reply_trailers);
    }
  }

  void closeStream() {
    grpc_stream_->closeStream();
    AssertionResult result = fake_stream_->waitForEndStream(dispatcher_helper_.dispatcher_);
    RELEASE_ASSERT(result, result.message());
  }

  DispatcherHelper& dispatcher_helper_;
  FakeStream* fake_stream_{};
  AsyncStream<helloworld::HelloRequest> grpc_stream_{};
  const TestMetadata empty_metadata_;
};

using HelloworldStreamPtr = std::unique_ptr<HelloworldStream>;

// Request related test utilities.
class HelloworldRequest : public MockAsyncRequestCallbacks<helloworld::HelloReply> {
public:
  HelloworldRequest(DispatcherHelper& dispatcher_helper) : dispatcher_helper_(dispatcher_helper) {}

  void sendReply() {
    fake_stream_->startGrpcStream();
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*child_span_, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("0")));
    EXPECT_CALL(*this, onSuccess_(HelloworldReplyEq(HELLO_REPLY), _)).WillExitIfNeeded();
    EXPECT_CALL(*child_span_, finishSpan());
    dispatcher_helper_.setStreamEventPending();
    fake_stream_->sendGrpcMessage(reply);
    fake_stream_->finishGrpcStream(Grpc::Status::Ok);
  }

  DispatcherHelper& dispatcher_helper_;
  FakeStream* fake_stream_{};
  AsyncRequest* grpc_request_{};
  Tracing::MockSpan* child_span_{new Tracing::MockSpan()};
};

using HelloworldRequestPtr = std::unique_ptr<HelloworldRequest>;

// Integration test base that can be used with time system variants.
template <class TimeSystemVariant> class GrpcClientIntegrationTestBase {
public:
  GrpcClientIntegrationTestBase()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        api_(Api::createApiForTest(stats_store_, time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        http_context_(stats_store_.symbolTable()), router_context_(stats_store_.symbolTable()) {}

  virtual Network::Address::IpVersion getIpVersion() const PURE;
  virtual ClientType getClientType() const PURE;

  virtual void initialize(uint32_t envoy_grpc_max_recv_msg_length = 0) {
    if (fake_upstream_ == nullptr) {
      fake_upstream_config_.upstream_protocol_ = Http::CodecType::HTTP2;
      fake_upstream_ = std::make_unique<FakeUpstream>(0, getIpVersion(), fake_upstream_config_);
    }
    switch (getClientType()) {
    case ClientType::EnvoyGrpc:
      grpc_client_ = createAsyncClientImpl(envoy_grpc_max_recv_msg_length);
      break;
    case ClientType::GoogleGrpc: {
      grpc_client_ = createGoogleAsyncClientImpl();
      break;
    }
    }
    // Setup a test timeout (also needed to maintain an active event in the dispatcher so that
    // .run() will block until timeout rather than exit immediately).
    timeout_timer_ = dispatcher_->createTimer([this] {
      FAIL() << "Test timeout";
      dispatcher_->exit();
    });
    timeout_timer_->enableTimer(std::chrono::milliseconds(10000));
  }

  virtual ~GrpcClientIntegrationTestBase() {
    if (fake_connection_) {
      AssertionResult result = fake_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_connection_.reset();
    }
  }

  void fillServiceWideInitialMetadata(envoy::config::core::v3::GrpcService& config) {
    for (const auto& item : service_wide_initial_metadata_) {
      auto* header_value = config.add_initial_metadata();
      header_value->set_key(item.first.get());
      header_value->set_value(item.second);
    }
  }

  // Create a Grpc::AsyncClientImpl instance backed by enough fake/mock
  // infrastructure to initiate a loopback TCP connection to fake_upstream_.
  RawAsyncClientPtr createAsyncClientImpl(uint32_t envoy_grpc_max_recv_msg_length = 0) {
    client_connection_ = std::make_unique<Network::ClientConnectionImpl>(
        *dispatcher_, fake_upstream_->localAddress(), nullptr,
        std::move(async_client_transport_socket_), nullptr, nullptr);
    if (connection_buffer_limits_ != 0) {
      client_connection_->setBufferLimits(connection_buffer_limits_);
    }

    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, connectTimeout())
        .WillByDefault(Return(std::chrono::milliseconds(10000)));
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm_, getThreadLocalCluster("fake_cluster")).Times(AtLeast(1));
    Upstream::MockHost::MockCreateConnectionData connection_data{client_connection_.release(),
                                                                 host_description_ptr_};
    EXPECT_CALL(*mock_host_, createConnection_(_, _)).WillRepeatedly(Return(connection_data));
    EXPECT_CALL(*mock_host_, cluster())
        .WillRepeatedly(ReturnRef(*cm_.thread_local_cluster_.cluster_.info_));
    EXPECT_CALL(*mock_host_description_, locality()).WillRepeatedly(ReturnRef(host_locality_));
    http_conn_pool_ = Http::Http2::allocateConnPool(*dispatcher_, api_->randomGenerator(),
                                                    host_ptr_, Upstream::ResourcePriority::Default,
                                                    nullptr, nullptr, state_);
    EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
        .WillRepeatedly(Return(Upstream::HttpPoolData([]() {}, http_conn_pool_.get())));
    http_async_client_ = std::make_unique<Http::AsyncClientImpl>(
        cm_.thread_local_cluster_.cluster_.info_, stats_store_, *dispatcher_, cm_,
        server_factory_context_, std::move(shadow_writer_ptr_), http_context_, router_context_);
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillRepeatedly(ReturnRef(*http_async_client_));
    envoy::config::core::v3::GrpcService config;
    config.mutable_envoy_grpc()->set_cluster_name("fake_cluster");
    if (envoy_grpc_max_recv_msg_length != 0) {
      config.mutable_envoy_grpc()->mutable_max_receive_message_length()->set_value(
          envoy_grpc_max_recv_msg_length);
    }

    config.mutable_envoy_grpc()->set_skip_envoy_headers(skip_envoy_headers_);

    fillServiceWideInitialMetadata(config);
    return std::make_unique<AsyncClientImpl>(cm_, config, dispatcher_->timeSource());
  }

  virtual envoy::config::core::v3::GrpcService createGoogleGrpcConfig() {
    envoy::config::core::v3::GrpcService config;
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_target_uri(fake_upstream_->localAddress()->asString());
    google_grpc->set_stat_prefix("fake_cluster");
    for (const auto& config_arg : channel_args_) {
      (*google_grpc->mutable_channel_args()->mutable_args())[config_arg.first].set_string_value(
          config_arg.second);
    }
    fillServiceWideInitialMetadata(config);
    return config;
  }

  RawAsyncClientPtr createGoogleAsyncClientImpl() {
#ifdef ENVOY_GOOGLE_GRPC
    google_tls_ = std::make_unique<GoogleAsyncClientThreadLocal>(*api_);
    GoogleGenericStubFactory stub_factory;
    return std::make_unique<GoogleAsyncClientImpl>(
        *dispatcher_, *google_tls_, stub_factory, stats_scope_, createGoogleGrpcConfig(),
        server_factory_context_, google_grpc_stat_names_);
#else
    PANIC("reached unexpected code");
#endif
  }

  void expectInitialHeaders(FakeStream& fake_stream, const TestMetadata& initial_metadata) {
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    stream_headers_ = std::make_unique<Http::TestRequestHeaderMapImpl>(fake_stream.headers());
    EXPECT_EQ("POST", stream_headers_->get_(":method"));
    EXPECT_EQ("/helloworld.Greeter/SayHello", stream_headers_->get_(":path"));
    EXPECT_EQ("application/grpc", stream_headers_->get_("content-type"));
    EXPECT_EQ("trailers", stream_headers_->get_("te"));

    // "x-envoy-internal" and `x-forward-for` headers are only available in envoy gRPC path.
    // They will be removed when either envoy gRPC config or stream option is false.
    if (getClientType() == ClientType::EnvoyGrpc) {
      if (!skip_envoy_headers_ && send_internal_header_stream_option_) {
        EXPECT_FALSE(stream_headers_->get_("x-envoy-internal").empty());
      } else {
        EXPECT_TRUE(stream_headers_->get_("x-envoy-internal").empty());
      }
      if (!skip_envoy_headers_ && send_xff_header_stream_option_) {
        EXPECT_FALSE(stream_headers_->get_("x-forwarded-for").empty());
      } else {
        EXPECT_TRUE(stream_headers_->get_("x-forwarded-for").empty());
      }
    }

    for (const auto& value : initial_metadata) {
      EXPECT_EQ(value.second, stream_headers_->get_(value.first));
    }
    for (const auto& value : service_wide_initial_metadata_) {
      EXPECT_EQ(value.second, stream_headers_->get_(value.first));
    }
  }

  virtual void expectExtraHeaders(FakeStream&) {}

  HelloworldRequestPtr createRequest(const TestMetadata& initial_metadata,
                                     bool expect_upstream_request = true) {
    auto request = std::make_unique<HelloworldRequest>(dispatcher_helper_);
    EXPECT_CALL(*request, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (const auto& value : initial_metadata) {
            headers.addReference(value.first, value.second);
          }
        }));
    helloworld::HelloRequest request_msg;
    request_msg.set_name(HELLO_REQUEST);

    Tracing::MockSpan active_span;
    EXPECT_CALL(active_span, spawnChild_(_, "async helloworld.Greeter.SayHello egress", _))
        .WillOnce(Return(request->child_span_));
    EXPECT_CALL(*request->child_span_,
                setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
    EXPECT_CALL(*request->child_span_,
                setTag(Eq(Tracing::Tags::get().UpstreamAddress), Not(IsEmpty())))
        .Times(AtMost(1));
    EXPECT_CALL(*request->child_span_,
                setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
    EXPECT_CALL(*request->child_span_, injectContext(_, _));

    request->grpc_request_ = grpc_client_->send(*method_descriptor_, request_msg, *request,
                                                active_span, Http::AsyncClient::RequestOptions());
    EXPECT_NE(request->grpc_request_, nullptr);

    if (!expect_upstream_request) {
      return request;
    }

    if (!fake_connection_) {
      AssertionResult result =
          fake_upstream_->waitForHttpConnection(*dispatcher_, fake_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    fake_streams_.emplace_back();
    AssertionResult result = fake_connection_->waitForNewStream(*dispatcher_, fake_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& fake_stream = *fake_streams_.back();
    request->fake_stream_ = &fake_stream;

    expectInitialHeaders(fake_stream, initial_metadata);
    expectExtraHeaders(fake_stream);

    helloworld::HelloRequest received_msg;
    result = fake_stream.waitForGrpcMessage(*dispatcher_, received_msg);
    RELEASE_ASSERT(result, result.message());
    EXPECT_THAT(request_msg, ProtoEq(received_msg));

    return request;
  }

  HelloworldStreamPtr createStream(const TestMetadata& initial_metadata) {
    auto stream = std::make_unique<HelloworldStream>(dispatcher_helper_);
    EXPECT_CALL(*stream, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (const auto& value : initial_metadata) {
            headers.addReference(value.first, value.second);
          }
        }));

    auto options = Http::AsyncClient::StreamOptions();
    envoy::config::core::v3::Metadata m;
    (*m.mutable_filter_metadata())["com.foo.bar"] = {};
    options.setMetadata(m);
    options.setSendInternal(send_internal_header_stream_option_);
    options.setSendXff(send_xff_header_stream_option_);
    stream->grpc_stream_ = grpc_client_->start(*method_descriptor_, *stream, options);
    EXPECT_NE(stream->grpc_stream_, nullptr);

    if (!fake_connection_) {
      AssertionResult result =
          fake_upstream_->waitForHttpConnection(*dispatcher_, fake_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    fake_streams_.emplace_back();
    AssertionResult result = fake_connection_->waitForNewStream(*dispatcher_, fake_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& fake_stream = *fake_streams_.back();
    stream->fake_stream_ = &fake_stream;

    expectInitialHeaders(fake_stream, initial_metadata);
    expectExtraHeaders(fake_stream);

    return stream;
  }

  Event::DelegatingTestTimeSystem<TimeSystemVariant> time_system_;
  FakeUpstreamConfig fake_upstream_config_{time_system_};
  std::unique_ptr<FakeUpstream> fake_upstream_;
  FakeHttpConnectionPtr fake_connection_;
  std::vector<FakeStreamPtr> fake_streams_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::IsolatedStoreImpl stats_store_{*symbol_table_};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DispatcherHelper dispatcher_helper_{*dispatcher_};
  Stats::ScopeSharedPtr stats_scope_{stats_store_.rootScope()};
  Grpc::StatNames google_grpc_stat_names_{stats_store_.symbolTable()};
  TestMetadata service_wide_initial_metadata_;
  std::unique_ptr<Http::TestRequestHeaderMapImpl> stream_headers_;
  std::vector<std::pair<std::string, std::string>> channel_args_;
#ifdef ENVOY_GOOGLE_GRPC
  GoogleAsyncClientThreadLocalPtr google_tls_;
#endif
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
  Event::TimerPtr timeout_timer_;
  const TestMetadata empty_metadata_;

  // Fake/mock infrastructure for Grpc::AsyncClientImpl upstream.
  Upstream::ClusterConnectivityState state_;
  Network::TransportSocketPtr async_client_transport_socket_{new Network::RawBufferSocket()};
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{server_factory_context_};
  Upstream::MockClusterManager& cm_{server_factory_context_.cluster_manager_};
  Http::AsyncClientPtr http_async_client_;
  Http::ConnectionPool::InstancePtr http_conn_pool_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  envoy::config::core::v3::Locality host_locality_;
  Upstream::MockHost* mock_host_ = new NiceMock<Upstream::MockHost>();
  Upstream::MockHostDescription* mock_host_description_ =
      new NiceMock<Upstream::MockHostDescription>();
  Upstream::HostDescriptionConstSharedPtr host_description_ptr_{mock_host_description_};
  Upstream::HostConstSharedPtr host_ptr_{mock_host_};
  Router::MockShadowWriter* mock_shadow_writer_ = new Router::MockShadowWriter();
  Router::ShadowWriterPtr shadow_writer_ptr_{mock_shadow_writer_};
  Network::ClientConnectionPtr client_connection_;
  bool skip_envoy_headers_{false};
  bool send_internal_header_stream_option_{true};
  bool send_xff_header_stream_option_{true};
  // Connection buffer limits, 0 means default limit from config is used.
  uint32_t connection_buffer_limits_{0};
};

// The integration test for Envoy gRPC and Google gRPC. It uses `TestRealTimeSystem`.
class GrpcClientIntegrationTest : public GrpcClientIntegrationParamTest,
                                  public GrpcClientIntegrationTestBase<Event::TestRealTimeSystem> {
public:
  virtual Network::Address::IpVersion getIpVersion() const override {
    return GrpcClientIntegrationParamTest::ipVersion();
  }
  virtual ClientType getClientType() const override {
    return GrpcClientIntegrationParamTest::clientType();
  };
};

// The integration test for Envoy gRPC flow control. It uses `SimulatedTime`.
class EnvoyGrpcFlowControlTest
    : public EnvoyGrpcClientIntegrationParamTest,
      public GrpcClientIntegrationTestBase<Event::SimulatedTimeSystemHelper> {
public:
  virtual Network::Address::IpVersion getIpVersion() const override {
    return EnvoyGrpcClientIntegrationParamTest::ipVersion();
  }
  virtual ClientType getClientType() const override {
    return EnvoyGrpcClientIntegrationParamTest::clientType();
  };
};

// SSL connection credential validation tests.
class GrpcSslClientIntegrationTest : public GrpcClientIntegrationTest {
public:
  GrpcSslClientIntegrationTest() {
    ON_CALL(factory_context_.server_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(server_factory_context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher_));
  }
  void TearDown() override {
    // Reset some state in the superclass before we destruct context_manager_ in our destructor, it
    // doesn't like dangling contexts at destruction.
    if (fake_connection_) {
      AssertionResult result = fake_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_connection_.reset();
    }
    fake_upstream_.reset();
    async_client_transport_socket_.reset();
    client_connection_.reset();
  }

  envoy::config::core::v3::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcClientIntegrationTest::createGoogleGrpcConfig();
    TestUtility::setTestSslGoogleGrpcConfig(config, use_client_cert_);
    return config;
  }

  void initialize(uint32_t envoy_grpc_max_recv_msg_length = 0) override {
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* validation_context = common_tls_context->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    if (use_client_cert_) {
      auto* tls_cert = common_tls_context->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
      tls_cert->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    }

    auto cfg = *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(
        tls_context, factory_context_);

    mock_host_description_->socket_factory_ =
        *Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
            std::move(cfg), context_manager_, *stats_store_.rootScope());
    async_client_transport_socket_ =
        mock_host_description_->socket_factory_->createTransportSocket(nullptr, nullptr);
    FakeUpstreamConfig config(time_system_);
    config.upstream_protocol_ = Http::CodecType::HTTP2;
    fake_upstream_ =
        std::make_unique<FakeUpstream>(createUpstreamSslContext(), 0, ipVersion(), config);

    GrpcClientIntegrationTest::initialize(envoy_grpc_max_recv_msg_length);
  }

  Network::DownstreamTransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
    if (use_client_cert_) {
      tls_context.mutable_require_client_certificate()->set_value(true);
      auto* validation_context = common_tls_context->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    }
    if (use_server_tls_13_) {
      auto* tls_params = common_tls_context->mutable_tls_params();
      tls_params->set_tls_minimum_protocol_version(
          envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
      tls_params->set_tls_maximum_protocol_version(
          envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
    }

    auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
        tls_context, factory_context_, false);

    static auto* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
        std::vector<std::string>{});
  }

  bool use_client_cert_{};
  bool use_server_tls_13_{false};
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

} // namespace
} // namespace Grpc
} // namespace Envoy
