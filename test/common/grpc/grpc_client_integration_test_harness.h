#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/stats/scope.h"

#include "common/api/api_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/grpc/async_client_impl.h"
#include "common/grpc/context_impl.h"
#include "common/http/context_impl.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "common/grpc/google_async_client_impl.h"
#endif

#include "common/http/async_client_impl.h"
#include "common/http/codes.h"
#include "common/http/http2/conn_pool.h"
#include "common/stats/symbol_table_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/raw_buffer_socket.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

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
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
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

// Stream related test utilities.
class HelloworldStream : public MockAsyncStreamCallbacks<helloworld::HelloReply> {
public:
  HelloworldStream(DispatcherHelper& dispatcher_helper) : dispatcher_helper_(dispatcher_helper) {}

  void sendRequest(bool end_stream = false) {
    helloworld::HelloRequest request_msg;
    request_msg.set_name(HELLO_REQUEST);
    grpc_stream_->sendMessage(request_msg, end_stream);

    helloworld::HelloRequest received_msg;
    AssertionResult result =
        fake_stream_->waitForGrpcMessage(dispatcher_helper_.dispatcher_, received_msg);
    RELEASE_ASSERT(result, result.message());
    EXPECT_THAT(request_msg, ProtoEq(received_msg));
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
    fake_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl(*reply_headers), false);
  }

  void sendReply() {
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*this, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY))).WillExitIfNeeded();
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

class GrpcClientIntegrationTest : public GrpcClientIntegrationParamTest {
public:
  GrpcClientIntegrationTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")),
        api_(Api::createApiForTest(*stats_store_, test_time_.timeSystem())),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        http_context_(stats_store_->symbolTable()) {}

  virtual void initialize() {
    if (fake_upstream_ == nullptr) {
      fake_upstream_ = std::make_unique<FakeUpstream>(0, FakeHttpConnection::Type::HTTP2,
                                                      ipVersion(), test_time_.timeSystem());
    }
    switch (clientType()) {
    case ClientType::EnvoyGrpc:
      grpc_client_ = createAsyncClientImpl();
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

  void TearDown() override {
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
  RawAsyncClientPtr createAsyncClientImpl() {
    client_connection_ = std::make_unique<Network::ClientConnectionImpl>(
        *dispatcher_, fake_upstream_->localAddress(), nullptr,
        std::move(async_client_transport_socket_), nullptr);
    ON_CALL(*mock_cluster_info_, connectTimeout())
        .WillByDefault(Return(std::chrono::milliseconds(10000)));
    EXPECT_CALL(*mock_cluster_info_, name()).WillRepeatedly(ReturnRef(fake_cluster_name_));
    EXPECT_CALL(cm_, get(_)).WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_ptr_));
    Upstream::MockHost::MockCreateConnectionData connection_data{client_connection_.release(),
                                                                 host_description_ptr_};
    EXPECT_CALL(*mock_host_, createConnection_(_, _)).WillRepeatedly(Return(connection_data));
    EXPECT_CALL(*mock_host_, cluster()).WillRepeatedly(ReturnRef(*cluster_info_ptr_));
    EXPECT_CALL(*mock_host_description_, locality()).WillRepeatedly(ReturnRef(host_locality_));
    http_conn_pool_ = std::make_unique<Http::Http2::ProdConnPoolImpl>(
        *dispatcher_, random_, host_ptr_, Upstream::ResourcePriority::Default, nullptr, nullptr);
    EXPECT_CALL(cm_, httpConnPoolForCluster(_, _, _, _))
        .WillRepeatedly(Return(http_conn_pool_.get()));
    http_async_client_ = std::make_unique<Http::AsyncClientImpl>(
        cluster_info_ptr_, *stats_store_, *dispatcher_, local_info_, cm_, runtime_, random_,
        std::move(shadow_writer_ptr_), http_context_);
    EXPECT_CALL(cm_, httpAsyncClientForCluster(fake_cluster_name_))
        .WillRepeatedly(ReturnRef(*http_async_client_));
    EXPECT_CALL(cm_, get(Eq(fake_cluster_name_))).WillRepeatedly(Return(&thread_local_cluster_));
    envoy::config::core::v3::GrpcService config;
    config.mutable_envoy_grpc()->set_cluster_name(fake_cluster_name_);
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
    return std::make_unique<GoogleAsyncClientImpl>(*dispatcher_, *google_tls_, stub_factory,
                                                   stats_scope_, createGoogleGrpcConfig(), *api_,
                                                   google_grpc_stat_names_);
#else
    NOT_REACHED_GCOVR_EXCL_LINE;
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
    for (const auto& value : initial_metadata) {
      EXPECT_EQ(value.second, stream_headers_->get_(value.first));
    }
    for (const auto& value : service_wide_initial_metadata_) {
      EXPECT_EQ(value.second, stream_headers_->get_(value.first));
    }
  }

  virtual void expectExtraHeaders(FakeStream&) {}

  HelloworldRequestPtr createRequest(const TestMetadata& initial_metadata) {
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
    EXPECT_CALL(active_span, spawnChild_(_, "async fake_cluster egress", _))
        .WillOnce(Return(request->child_span_));
    EXPECT_CALL(*request->child_span_,
                setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq(fake_cluster_name_)));
    EXPECT_CALL(*request->child_span_,
                setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
    EXPECT_CALL(*request->child_span_, injectContext(_));

    request->grpc_request_ = grpc_client_->send(*method_descriptor_, request_msg, *request,
                                                active_span, Http::AsyncClient::RequestOptions());
    EXPECT_NE(request->grpc_request_, nullptr);

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

    stream->grpc_stream_ =
        grpc_client_->start(*method_descriptor_, *stream, Http::AsyncClient::StreamOptions());
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

  DangerousDeprecatedTestTime test_time_;
  std::unique_ptr<FakeUpstream> fake_upstream_;
  FakeHttpConnectionPtr fake_connection_;
  std::vector<FakeStreamPtr> fake_streams_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Stats::TestSymbolTable symbol_table_;
  Stats::IsolatedStoreImpl* stats_store_ = new Stats::IsolatedStoreImpl(*symbol_table_);
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DispatcherHelper dispatcher_helper_{*dispatcher_};
  Stats::ScopeSharedPtr stats_scope_{stats_store_};
  Grpc::StatNames google_grpc_stat_names_{stats_store_->symbolTable()};
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
  Network::TransportSocketPtr async_client_transport_socket_{new Network::RawBufferSocket()};
  const std::string fake_cluster_name_{"fake_cluster"};
  Upstream::MockClusterManager cm_;
  Upstream::MockClusterInfo* mock_cluster_info_ = new NiceMock<Upstream::MockClusterInfo>();
  Upstream::ClusterInfoConstSharedPtr cluster_info_ptr_{mock_cluster_info_};
  Upstream::MockThreadLocalCluster thread_local_cluster_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Runtime::MockLoader runtime_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_{test_time_.timeSystem()};
  NiceMock<Random::MockRandomGenerator> random_;
  Http::AsyncClientPtr http_async_client_;
  Http::ConnectionPool::InstancePtr http_conn_pool_;
  Http::ContextImpl http_context_;
  envoy::config::core::v3::Locality host_locality_;
  Upstream::MockHost* mock_host_ = new NiceMock<Upstream::MockHost>();
  Upstream::MockHostDescription* mock_host_description_ =
      new NiceMock<Upstream::MockHostDescription>();
  Upstream::HostDescriptionConstSharedPtr host_description_ptr_{mock_host_description_};
  Upstream::HostConstSharedPtr host_ptr_{mock_host_};
  Router::MockShadowWriter* mock_shadow_writer_ = new Router::MockShadowWriter();
  Router::ShadowWriterPtr shadow_writer_ptr_{mock_shadow_writer_};
  Network::ClientConnectionPtr client_connection_;
};

// SSL connection credential validation tests.
class GrpcSslClientIntegrationTest : public GrpcClientIntegrationTest {
public:
  GrpcSslClientIntegrationTest() {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }
  void TearDown() override {
    // Reset some state in the superclass before we destruct context_manager_ in our destructor, it
    // doesn't like dangling contexts at destruction.
    GrpcClientIntegrationTest::TearDown();
    fake_upstream_.reset();
    async_client_transport_socket_.reset();
    client_connection_.reset();
  }

  envoy::config::core::v3::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcClientIntegrationTest::createGoogleGrpcConfig();
    TestUtility::setTestSslGoogleGrpcConfig(config, use_client_cert_);
    return config;
  }

  void initialize() override {
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
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
        tls_context, factory_context_);

    mock_host_description_->socket_factory_ =
        std::make_unique<Extensions::TransportSockets::Tls::ClientSslSocketFactory>(
            std::move(cfg), context_manager_, *stats_store_);
    async_client_transport_socket_ =
        mock_host_description_->socket_factory_->createTransportSocket(nullptr);
    fake_upstream_ = std::make_unique<FakeUpstream>(createUpstreamSslContext(), 0,
                                                    FakeHttpConnection::Type::HTTP2, ipVersion(),
                                                    test_time_.timeSystem());

    GrpcClientIntegrationTest::initialize();
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
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

    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);

    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  bool use_client_cert_{};
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};

} // namespace
} // namespace Grpc
} // namespace Envoy
