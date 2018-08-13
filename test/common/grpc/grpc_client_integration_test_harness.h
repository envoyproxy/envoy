#include "envoy/stats/scope.h"

#include "common/event/dispatcher_impl.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/http2/conn_pool.h"
#include "common/network/connection_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/ssl/context_config_impl.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/grpc/utility.h"
#include "test/integration/fake_upstream.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/environment.h"

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Grpc {
namespace {

const char HELLO_REQUEST[] = "ABC";
const char HELLO_REPLY[] = "DEFG";

MATCHER_P(HelloworldReplyEq, rhs, "") { return arg.message() == rhs; }

typedef std::vector<std::pair<Http::LowerCaseString, std::string>> TestMetadata;

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
          Http::TestHeaderMapImpl stream_headers(received_headers);
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
          Http::TestHeaderMapImpl stream_headers(received_headers);
          for (auto& value : metadata) {
            EXPECT_EQ(value.second, stream_headers.get_(value.first));
          }
          dispatcher_helper_.exitDispatcherIfNeeded();
        }));
    dispatcher_helper_.setStreamEventPending();
  }

  void sendServerInitialMetadata(const TestMetadata& metadata) {
    Http::HeaderMapPtr reply_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
    for (auto& value : metadata) {
      reply_headers->addReference(value.first, value.second);
    }
    expectInitialMetadata(metadata);
    fake_stream_->encodeHeaders(*reply_headers, false);
  }

  void sendReply() {
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*this, onReceiveMessage_(HelloworldReplyEq(HELLO_REPLY))).WillExitIfNeeded();
    dispatcher_helper_.setStreamEventPending();
    fake_stream_->sendGrpcMessage<helloworld::HelloReply>(reply);
  }

  void expectGrpcStatus(Status::GrpcStatus grpc_status) {
    if (grpc_status == Status::GrpcStatus::InvalidCode) {
      EXPECT_CALL(*this, onRemoteClose(_, _)).WillExitIfNeeded();
    } else {
      EXPECT_CALL(*this, onRemoteClose(grpc_status, _)).WillExitIfNeeded();
    }
    dispatcher_helper_.setStreamEventPending();
  }

  void sendServerTrailers(Status::GrpcStatus grpc_status, const std::string& grpc_message,
                          const TestMetadata& metadata, bool trailers_only = false) {
    Http::TestHeaderMapImpl reply_trailers{{"grpc-status", std::to_string(enumToInt(grpc_status))}};
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
  AsyncStream* grpc_stream_{};
  const TestMetadata empty_metadata_;
};

// Request related test utilities.
class HelloworldRequest : public MockAsyncRequestCallbacks<helloworld::HelloReply> {
public:
  HelloworldRequest(DispatcherHelper& dispatcher_helper) : dispatcher_helper_(dispatcher_helper) {}

  void sendReply() {
    fake_stream_->startGrpcStream();
    helloworld::HelloReply reply;
    reply.set_message(HELLO_REPLY);
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().GRPC_STATUS_CODE, "0"));
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

class GrpcClientIntegrationTest : public GrpcClientIntegrationParamTest {
public:
  GrpcClientIntegrationTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {}

  virtual void initialize() {
    if (fake_upstream_ == nullptr) {
      fake_upstream_ =
          std::make_unique<FakeUpstream>(0, FakeHttpConnection::Type::HTTP2, ipVersion());
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
    timeout_timer_ = dispatcher_.createTimer([this] {
      FAIL() << "Test timeout";
      dispatcher_.exit();
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

  void fillServiceWideInitialMetadata(envoy::api::v2::core::GrpcService& config) {
    for (const auto& item : service_wide_initial_metadata_) {
      auto* header_value = config.add_initial_metadata();
      header_value->set_key(item.first.get());
      header_value->set_value(item.second);
    }
  }

  // Create a Grpc::AsyncClientImpl instance backed by enough fake/mock
  // infrastructure to initiate a loopback TCP connection to fake_upstream_.
  AsyncClientPtr createAsyncClientImpl() {
    client_connection_ = std::make_unique<Network::ClientConnectionImpl>(
        dispatcher_, fake_upstream_->localAddress(), nullptr,
        std::move(async_client_transport_socket_), nullptr);
    ON_CALL(*mock_cluster_info_, connectTimeout())
        .WillByDefault(Return(std::chrono::milliseconds(1000)));
    EXPECT_CALL(*mock_cluster_info_, name()).WillRepeatedly(ReturnRef(fake_cluster_name_));
    EXPECT_CALL(cm_, get(_)).WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info_ptr_));
    Upstream::MockHost::MockCreateConnectionData connection_data{client_connection_.release(),
                                                                 host_description_ptr_};
    EXPECT_CALL(*mock_host_, createConnection_(_, _)).WillRepeatedly(Return(connection_data));
    EXPECT_CALL(*mock_host_, cluster()).WillRepeatedly(ReturnRef(*cluster_info_ptr_));
    EXPECT_CALL(*mock_host_description_, locality()).WillRepeatedly(ReturnRef(host_locality_));
    http_conn_pool_ = std::make_unique<Http::Http2::ProdConnPoolImpl>(
        dispatcher_, host_ptr_, Upstream::ResourcePriority::Default, nullptr);
    EXPECT_CALL(cm_, httpConnPoolForCluster(_, _, _, _))
        .WillRepeatedly(Return(http_conn_pool_.get()));
    http_async_client_ = std::make_unique<Http::AsyncClientImpl>(
        *cluster_info_ptr_, *stats_store_, dispatcher_, local_info_, cm_, runtime_, random_,
        std::move(shadow_writer_ptr_));
    EXPECT_CALL(cm_, httpAsyncClientForCluster(fake_cluster_name_))
        .WillRepeatedly(ReturnRef(*http_async_client_));
    EXPECT_CALL(cm_, get(fake_cluster_name_)).WillRepeatedly(Return(&thread_local_cluster_));
    envoy::api::v2::core::GrpcService config;
    config.mutable_envoy_grpc()->set_cluster_name(fake_cluster_name_);
    fillServiceWideInitialMetadata(config);
    return std::make_unique<AsyncClientImpl>(cm_, config);
  }

  virtual envoy::api::v2::core::GrpcService createGoogleGrpcConfig() {
    envoy::api::v2::core::GrpcService config;
    auto* google_grpc = config.mutable_google_grpc();
    google_grpc->set_target_uri(fake_upstream_->localAddress()->asString());
    google_grpc->set_stat_prefix("fake_cluster");
    fillServiceWideInitialMetadata(config);
    return config;
  }

  AsyncClientPtr createGoogleAsyncClientImpl() {
#ifdef ENVOY_GOOGLE_GRPC
    google_tls_ = std::make_unique<GoogleAsyncClientThreadLocal>();
    GoogleGenericStubFactory stub_factory;
    return std::make_unique<GoogleAsyncClientImpl>(dispatcher_, *google_tls_, stub_factory,
                                                   stats_scope_, createGoogleGrpcConfig());
#else
    NOT_REACHED_GCOVR_EXCL_LINE;
#endif
  }

  void expectInitialHeaders(FakeStream& fake_stream, const TestMetadata& initial_metadata) {
    AssertionResult result = fake_stream.waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    Http::TestHeaderMapImpl stream_headers(fake_stream.headers());
    EXPECT_EQ("POST", stream_headers.get_(":method"));
    EXPECT_EQ("/helloworld.Greeter/SayHello", stream_headers.get_(":path"));
    EXPECT_EQ("application/grpc", stream_headers.get_("content-type"));
    EXPECT_EQ("trailers", stream_headers.get_("te"));
    for (const auto& value : initial_metadata) {
      EXPECT_EQ(value.second, stream_headers.get_(value.first));
    }
    for (const auto& value : service_wide_initial_metadata_) {
      EXPECT_EQ(value.second, stream_headers.get_(value.first));
    }
  }

  virtual void expectExtraHeaders(FakeStream&) {}

  std::unique_ptr<HelloworldRequest> createRequest(const TestMetadata& initial_metadata) {
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
                setTag(Tracing::Tags::get().UPSTREAM_CLUSTER, fake_cluster_name_));
    EXPECT_CALL(*request->child_span_,
                setTag(Tracing::Tags::get().COMPONENT, Tracing::Tags::get().PROXY));
    EXPECT_CALL(*request->child_span_, injectContext(_));

    request->grpc_request_ =
        grpc_client_->send(*method_descriptor_, request_msg, *request, active_span,
                           absl::optional<std::chrono::milliseconds>());
    EXPECT_NE(request->grpc_request_, nullptr);

    if (!fake_connection_) {
      AssertionResult result = fake_upstream_->waitForHttpConnection(dispatcher_, fake_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    fake_streams_.emplace_back();
    AssertionResult result = fake_connection_->waitForNewStream(dispatcher_, fake_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& fake_stream = *fake_streams_.back();
    request->fake_stream_ = &fake_stream;

    expectInitialHeaders(fake_stream, initial_metadata);
    expectExtraHeaders(fake_stream);

    helloworld::HelloRequest received_msg;
    result = fake_stream.waitForGrpcMessage(dispatcher_, received_msg);
    RELEASE_ASSERT(result, result.message());
    EXPECT_THAT(request_msg, ProtoEq(received_msg));

    return request;
  }

  std::unique_ptr<HelloworldStream> createStream(const TestMetadata& initial_metadata) {
    auto stream = std::make_unique<HelloworldStream>(dispatcher_helper_);
    EXPECT_CALL(*stream, onCreateInitialMetadata(_))
        .WillOnce(Invoke([&initial_metadata](Http::HeaderMap& headers) {
          for (const auto& value : initial_metadata) {
            headers.addReference(value.first, value.second);
          }
        }));

    stream->grpc_stream_ = grpc_client_->start(*method_descriptor_, *stream);
    EXPECT_NE(stream->grpc_stream_, nullptr);

    if (!fake_connection_) {
      AssertionResult result = fake_upstream_->waitForHttpConnection(dispatcher_, fake_connection_);
      RELEASE_ASSERT(result, result.message());
    }
    fake_streams_.emplace_back();
    AssertionResult result = fake_connection_->waitForNewStream(dispatcher_, fake_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& fake_stream = *fake_streams_.back();
    stream->fake_stream_ = &fake_stream;

    expectInitialHeaders(fake_stream, initial_metadata);
    expectExtraHeaders(fake_stream);

    return stream;
  }

  std::unique_ptr<FakeUpstream> fake_upstream_;
  FakeHttpConnectionPtr fake_connection_;
  std::vector<FakeStreamPtr> fake_streams_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Event::DispatcherImpl dispatcher_;
  DispatcherHelper dispatcher_helper_{dispatcher_};
  Stats::IsolatedStoreImpl* stats_store_ = new Stats::IsolatedStoreImpl();
  Stats::ScopeSharedPtr stats_scope_{stats_store_};
  TestMetadata service_wide_initial_metadata_;
#ifdef ENVOY_GOOGLE_GRPC
  std::unique_ptr<GoogleAsyncClientThreadLocal> google_tls_;
#endif
  AsyncClientPtr grpc_client_;
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
  Ssl::ContextManagerImpl context_manager_{runtime_};
  NiceMock<Runtime::MockRandomGenerator> random_;
  Http::AsyncClientPtr http_async_client_;
  Http::ConnectionPool::InstancePtr http_conn_pool_;
  envoy::api::v2::core::Locality host_locality_;
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
  void TearDown() override {
    // Reset some state in the superclass before we destruct context_manager_ in our destructor, it
    // doesn't like dangling contexts at destruction.
    GrpcClientIntegrationTest::TearDown();
    fake_upstream_.reset();
    async_client_transport_socket_.reset();
    client_connection_.reset();
    mock_cluster_info_->transport_socket_factory_.reset();
  }

  virtual envoy::api::v2::core::GrpcService createGoogleGrpcConfig() override {
    auto config = GrpcClientIntegrationTest::createGoogleGrpcConfig();
    TestUtility::setTestSslGoogleGrpcConfig(config, use_client_cert_);
    return config;
  }

  void initialize() override {
    envoy::api::v2::auth::UpstreamTlsContext tls_context;
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
    auto cfg = std::make_unique<Ssl::ClientContextConfigImpl>(tls_context, secret_manager_);

    mock_cluster_info_->transport_socket_factory_ = std::make_unique<Ssl::ClientSslSocketFactory>(
        std::move(cfg), context_manager_, *stats_store_);
    ON_CALL(*mock_cluster_info_, transportSocketFactory())
        .WillByDefault(ReturnRef(*mock_cluster_info_->transport_socket_factory_));
    async_client_transport_socket_ =
        mock_cluster_info_->transport_socket_factory_->createTransportSocket();
    fake_upstream_ = std::make_unique<FakeUpstream>(createUpstreamSslContext(), 0,
                                                    FakeHttpConnection::Type::HTTP2, ipVersion());

    GrpcClientIntegrationTest::initialize();
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::api::v2::auth::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    common_tls_context->add_alpn_protocols("h2");
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

    auto cfg = std::make_unique<Ssl::ServerContextConfigImpl>(tls_context, secret_manager_);

    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Ssl::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  bool use_client_cert_{};
  NiceMock<Secret::MockSecretManager> secret_manager_;
};

} // namespace
} // namespace Grpc
} // namespace Envoy
