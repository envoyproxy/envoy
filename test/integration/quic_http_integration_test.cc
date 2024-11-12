#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <cstddef>
#include <initializer_list>
#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/quic/connection_debug_visitor/v3/connection_debug_visitor_basic.pb.h"
#include "envoy/extensions/quic/server_preferred_address/v3/fixed_server_preferred_address_config.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "source/common/quic/active_quic_listener.h"
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_client_transport_socket_factory.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/context_config_impl.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/common/quic/test_utils.h"
#include "test/common/tls/cert_validator/timed_cert_validator.h"
#include "test/common/upstream/utility.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/utility.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/http_integration.h"
#include "test/integration/socket_interface_swap.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {

using Extensions::TransportSockets::Tls::ContextImplPeer;

namespace Quic {

class CodecClientCallbacksForTest : public Http::CodecClientCallbacks {
public:
  void onStreamDestroy() override {}

  void onStreamReset(Http::StreamResetReason reason) override {
    last_stream_reset_reason_ = reason;
  }

  Http::StreamResetReason last_stream_reset_reason_{Http::StreamResetReason::LocalReset};
};

// This class enables testing on QUIC path validation
class TestEnvoyQuicClientConnection : public EnvoyQuicClientConnection {
public:
  TestEnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                                Network::Address::InstanceConstSharedPtr& initial_peer_address,
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Network::Address::InstanceConstSharedPtr local_addr,
                                Event::Dispatcher& dispatcher,
                                const Network::ConnectionSocket::OptionsSharedPtr& options,
                                bool validation_failure_on_path_response,
                                quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicClientConnection(server_connection_id, initial_peer_address, helper, alarm_factory,
                                  supported_versions, local_addr, dispatcher, options, generator,
                                  /*prefer_gro=*/true),
        dispatcher_(dispatcher),
        validation_failure_on_path_response_(validation_failure_on_path_response) {}

  AssertionResult
  waitForPathResponse(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    bool timer_fired = false;
    if (!saw_path_response_) {
      Event::TimerPtr timer(dispatcher_.createTimer([this, &timer_fired]() -> void {
        timer_fired = true;
        dispatcher_.exit();
      }));
      timer->enableTimer(timeout);
      waiting_for_path_response_ = true;
      dispatcher_.run(Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for path response\n";
      }
    }
    waiting_for_path_response_ = false;
    saw_path_response_ = false;
    return AssertionSuccess();
  }

  bool OnPathResponseFrame(const quic::QuicPathResponseFrame& frame) override {
    saw_path_response_ = true;
    if (waiting_for_path_response_) {
      dispatcher_.exit();
    }
    if (!validation_failure_on_path_response_) {
      return EnvoyQuicClientConnection::OnPathResponseFrame(frame);
    }
    CancelPathValidation();
    return connected();
  }

  AssertionResult
  waitForHandshakeDone(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    bool timer_fired = false;
    if (!saw_handshake_done_) {
      Event::TimerPtr timer(dispatcher_.createTimer([this, &timer_fired]() -> void {
        timer_fired = true;
        dispatcher_.exit();
      }));
      timer->enableTimer(timeout);
      waiting_for_handshake_done_ = true;
      dispatcher_.run(Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for handshake done\n";
      }
    }
    return AssertionSuccess();
  }

  bool OnHandshakeDoneFrame(const quic::QuicHandshakeDoneFrame& frame) override {
    saw_handshake_done_ = true;
    if (waiting_for_handshake_done_) {
      dispatcher_.exit();
    }
    return EnvoyQuicClientConnection::OnHandshakeDoneFrame(frame);
  }

  AssertionResult waitForNewCid(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    bool timer_fired = false;
    if (!saw_new_cid_) {
      Event::TimerPtr timer(dispatcher_.createTimer([this, &timer_fired]() -> void {
        timer_fired = true;
        dispatcher_.exit();
      }));
      timer->enableTimer(timeout);
      waiting_for_new_cid_ = true;
      dispatcher_.run(Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for new cid\n";
      }
    }
    waiting_for_new_cid_ = false;
    return AssertionSuccess();
  }

  bool OnNewConnectionIdFrame(const quic::QuicNewConnectionIdFrame& frame) override {
    bool ret = EnvoyQuicClientConnection::OnNewConnectionIdFrame(frame);
    saw_new_cid_ = true;
    if (waiting_for_new_cid_) {
      dispatcher_.exit();
    }
    saw_new_cid_ = false;
    return ret;
  }

  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::RawSlice saved_cmsg) override {
    last_local_address_ = local_address;
    last_peer_address_ = peer_address;
    EnvoyQuicClientConnection::processPacket(local_address, peer_address, std::move(buffer),
                                             receive_time, tos, saved_cmsg);
  }

  Network::Address::InstanceConstSharedPtr getLastLocalAddress() const {
    return last_local_address_;
  }

  Network::Address::InstanceConstSharedPtr getLastPeerAddress() const { return last_peer_address_; }

private:
  Event::Dispatcher& dispatcher_;
  bool saw_path_response_{false};
  bool saw_handshake_done_{false};
  bool saw_new_cid_{false};
  bool waiting_for_path_response_{false};
  bool waiting_for_handshake_done_{false};
  bool waiting_for_new_cid_{false};
  bool validation_failure_on_path_response_{false};
  Network::Address::InstanceConstSharedPtr last_local_address_;
  Network::Address::InstanceConstSharedPtr last_peer_address_;
};

// A test that sets up its own client connection with customized quic version and connection ID.
class QuicHttpIntegrationTestBase : public HttpIntegrationTest {
public:
  QuicHttpIntegrationTestBase(Network::Address::IpVersion version, std::string config)
      : HttpIntegrationTest(Http::CodecType::HTTP3, version, config),
        supported_versions_(quic::CurrentSupportedHttp3Versions()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()) {}

  ~QuicHttpIntegrationTestBase() override {
    cleanupUpstreamAndDownstream();
    // Release the client before destroying |conn_helper_|. No such need once |conn_helper_| is
    // moved into a client connection factory in the base test class.
    codec_client_.reset();
  }

  Network::ClientConnectionPtr makeClientConnectionWithOptions(
      uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    // Setting socket options is not supported.
    return makeClientConnectionWithHost(port, "", options);
  }

  Network::ClientConnectionPtr makeClientConnectionWithHost(
      uint32_t port, const std::string& host,
      const Network::ConnectionSocket::OptionsSharedPtr& options = nullptr) {
    // Setting socket options is not supported.
    server_addr_ = *Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Address::InstanceConstSharedPtr local_addr =
        Network::Test::getCanonicalLoopbackAddress(version_);
    // Initiate a QUIC connection with the highest supported version. If not
    // supported by server, this connection will fail.
    // TODO(danzh) Implement retry upon version mismatch and modify test frame work to specify a
    // different version set on server side to test that.
    auto connection = std::make_unique<TestEnvoyQuicClientConnection>(
        getNextConnectionId(), server_addr_, conn_helper_, alarm_factory_,
        quic::ParsedQuicVersionVector{supported_versions_[0]}, local_addr, *dispatcher_, options,
        validation_failure_on_path_response_, connection_id_generator_);
    quic_connection_ = connection.get();
    ASSERT(quic_connection_persistent_info_ != nullptr);
    auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
    OptRef<Http::HttpServerPropertiesCache> cache;
    auto session = std::make_unique<EnvoyQuicClientSession>(
        persistent_info.quic_config_, supported_versions_, std::move(connection),
        quic::QuicServerId{
            (host.empty() ? transport_socket_factory_->clientContextConfig()->serverNameIndication()
                          : host),
            static_cast<uint16_t>(port)},
        transport_socket_factory_->getCryptoConfig(), *dispatcher_,
        // Use smaller window than the default one to have test coverage of client codec buffer
        // exceeding high watermark.
        /*send_buffer_limit=*/2 * Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE,
        persistent_info.crypto_stream_factory_, quic_stat_names_, cache, *stats_store_.rootScope(),
        nullptr, *transport_socket_factory_);
    return session;
  }

  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options,
      absl::optional<envoy::config::core::v3::HttpProtocolOptions> common_http_options =
          absl::nullopt,
      bool wait_till_connected = true) override {
    ENVOY_LOG(debug, "Creating a new client {}",
              conn->connectionInfoProvider().localAddress()->asStringView());
    ASSERT(!common_http_options.has_value(), "Not implemented");
    return makeRawHttp3Connection(std::move(conn), http2_options, wait_till_connected);
  }

  // Create Http3 codec client with the option not to wait for 1-RTT key establishment.
  IntegrationCodecClientPtr makeRawHttp3Connection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options,
      bool wait_for_1rtt_key) {
    std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
    cluster->max_response_headers_count_ = 200;
    if (http2_options.has_value()) {
      cluster->http3_options_ = ConfigHelper::http2ToHttp3ProtocolOptions(
          http2_options.value(), quic::kStreamReceiveWindowLimit);
    }
    *cluster->http3_options_.mutable_quic_protocol_options() = client_quic_options_;
    Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
        cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)),
        timeSystem())};
    // This call may fail in QUICHE because of INVALID_VERSION. QUIC connection doesn't support
    // in-connection version negotiation.
    auto codec = std::make_unique<IntegrationCodecClient>(*dispatcher_, random_, std::move(conn),
                                                          host_description, downstream_protocol_,
                                                          wait_for_1rtt_key);
    if (codec->disconnected()) {
      // Connection may get closed during version negotiation or handshake.
      // TODO(#8479) QUIC connection doesn't support in-connection version negotiationPropagate
      // INVALID_VERSION error to caller and let caller to use server advertised version list to
      // create a new connection with mutually supported version and make client codec again.
      ENVOY_LOG(error, "Fail to connect to server with error: {}",
                codec->connection()->transportFailureReason());
      return codec;
    }
    codec->setCodecClientCallbacks(client_codec_callback_);
    return codec;
  }

  quic::QuicConnectionId getNextConnectionId() {
    if (designated_connection_ids_.empty()) {
      return quic::QuicUtils::CreateRandomConnectionId();
    }
    quic::QuicConnectionId cid = designated_connection_ids_.front();
    designated_connection_ids_.pop_front();
    return cid;
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_drain_timeout()->clear_seconds();
          hcm.mutable_drain_timeout()->set_nanos(500 * 1000 * 1000);
          EXPECT_EQ(hcm.codec_type(), envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager::HTTP3);
        });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});

    // Initialize the transport socket factory using a customized ssl option.
    ssl_client_option_.setSan(san_to_match_).setSni("lyft.com");
    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context;
    ON_CALL(context.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(context, statsScope()).WillByDefault(testing::ReturnRef(stats_scope_));
    ON_CALL(context, sslContextManager()).WillByDefault(testing::ReturnRef(context_manager_));
    ON_CALL(context.server_context_, threadLocal())
        .WillByDefault(testing::ReturnRef(thread_local_));
    envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport
        quic_transport_socket_config;
    auto* tls_context = quic_transport_socket_config.mutable_upstream_tls_context();
    initializeUpstreamTlsContextConfig(ssl_client_option_, *tls_context);
    tls_context->mutable_common_tls_context()->add_alpn_protocols(client_alpn_);

    envoy::config::core::v3::TransportSocket message;
    message.mutable_typed_config()->PackFrom(quic_transport_socket_config);
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(message);
    transport_socket_factory_.reset(static_cast<QuicClientTransportSocketFactory*>(
        config_factory.createTransportSocketFactory(quic_transport_socket_config, context)
            .value()
            .release()));
    ASSERT(transport_socket_factory_->clientContextConfig());
  }

  void setConcurrency(size_t concurrency) {
    concurrency_ = concurrency;
    if (concurrency > 1) {
      config_helper_.addConfigModifier(
          [=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
            // SO_REUSEPORT is needed because concurrency > 1.
            bootstrap.mutable_static_resources()
                ->mutable_listeners(0)
                ->mutable_enable_reuse_port()
                ->set_value(true);
          });
    }
  }

  void testMultipleUpstreamQuicConnections() {
    // As with testMultipleQuicConnections this function may be flaky when run with
    // --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);

    // Avoid having to figure out which requests land on which connections by
    // having one request per upstream connection.
    setUpstreamProtocol(Http::CodecType::HTTP3);
    envoy::config::listener::v3::QuicProtocolOptions options;
    options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(1);
    mergeOptions(options);

    initialize();
    std::vector<IntegrationCodecClientPtr> codec_clients;
    std::vector<IntegrationStreamDecoderPtr> responses;
    std::vector<FakeStreamPtr> upstream_requests;
    std::vector<FakeHttpConnectionPtr> upstream_connections;

    // Create |concurrency| clients
    size_t num_requests = concurrency_ * 2;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // See testMultipleQuicConnections for why this should result in connection spread.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      codec_clients.push_back(makeHttpConnection(lookupPort("http")));
    }

    // Create |num_requests| requests and wait for them to be received upstream
    for (size_t i = 0; i < num_requests; ++i) {
      responses.push_back(
          codec_clients[i % concurrency_]->makeHeaderOnlyRequest(default_request_headers_));
      waitForNextUpstreamRequest();
      ASSERT(upstream_request_ != nullptr);
      upstream_connections.push_back(std::move(fake_upstream_connection_));
      upstream_requests.push_back(std::move(upstream_request_));
    }

    // Send |num_requests| responses as fast as possible to regression test
    // against a prior credentials insert race.
    for (size_t i = 0; i < num_requests; ++i) {
      upstream_requests[i]->encodeHeaders(default_response_headers_, true);
    }

    // Wait for |num_requests| responses to complete.
    for (size_t i = 0; i < num_requests; ++i) {
      ASSERT_TRUE(responses[i]->waitForEndStream());
      EXPECT_TRUE(responses[i]->complete());
    }

    // Close |concurrency| clients
    for (size_t i = 0; i < concurrency_; ++i) {
      codec_clients[i]->close();
    }
  }

  void testMultipleQuicConnections() {
    autonomous_upstream_ = true;
    // Enabling SO_REUSEPORT with 8 workers. Unfortunately this setting makes the test rarely flaky
    // if it is configured to run with --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);
    initialize();
    std::vector<IntegrationCodecClientPtr> codec_clients;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection id
      // in the packet header. And currently all QUIC versions support >= 8 bytes connection id. So
      // create connections with the first 4 bytes of connection id different from each
      // other so they should be evenly distributed.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      // TODO(sunjayBhatia,wrowe): deserialize this, establishing all connections in parallel
      // (Expected to save ~14s each across 6 tests on Windows)
      codec_clients.push_back(makeHttpConnection(lookupPort("http")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10);
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_total", 8u, timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_total", 8u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (version_ == Network::Address::IpVersion::v4) {
        test_server_->waitForGaugeEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      } else {
        test_server_->waitForGaugeEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      }
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      auto encoder_decoder = codec_clients[i]->startRequest(default_request_headers_);

      auto& request_encoder = encoder_decoder.first;
      auto response = std::move(encoder_decoder.second);
      codec_clients[i]->sendData(request_encoder, 1000, true);
      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(response->complete());
      codec_clients[i]->close();
    }
  }

protected:
  quic::ParsedQuicVersionVector supported_versions_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  CodecClientCallbacksForTest client_codec_callback_;
  Network::Address::InstanceConstSharedPtr server_addr_;
  envoy::config::core::v3::QuicProtocolOptions client_quic_options_;
  TestEnvoyQuicClientConnection* quic_connection_{nullptr};
  std::list<quic::QuicConnectionId> designated_connection_ids_;
  Ssl::ClientSslTransportOptions ssl_client_option_;
  std::unique_ptr<Quic::QuicClientTransportSocketFactory> transport_socket_factory_;
  bool validation_failure_on_path_response_{false};
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  std::string client_alpn_;
};

class QuicHttpIntegrationTest : public QuicHttpIntegrationTestBase,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHttpIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig()) {}
};

class QuicHttpMultiAddressesIntegrationTest
    : public QuicHttpIntegrationTestBase,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHttpMultiAddressesIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig(true)) {}

  void testMultipleQuicConnections() {
    // Enabling SO_REUSEPORT with 8 workers. Unfortunately this setting makes the test rarely flaky
    // if it is configured to run with --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);
    initialize();
    std::vector<std::string> addresses({"address1", "address2"});
    registerTestServerPorts(addresses);
    std::vector<IntegrationCodecClientPtr> codec_clients1;
    std::vector<IntegrationCodecClientPtr> codec_clients2;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection id
      // in the packet header. And currently all QUIC versions support >= 8 bytes connection id. So
      // create connections with the first 4 bytes of connection id different from each
      // other so they should be evenly distributed.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      // TODO(sunjayBhatia,wrowe): deserialize this, establishing all connections in parallel
      // (Expected to save ~14s each across 6 tests on Windows)
      codec_clients1.push_back(makeHttpConnection(lookupPort("address1")));

      // Using the same connection id for the address1 and address2 here. Since the multiple
      // addresses listener are expected to create separated `ActiveQuicListener` instance for each
      // address, then expects the `UdpWorkerRouter` will route the connection to the correct
      // `ActiveQuicListener` instance. If the two connections with same connection id are going to
      // the same `ActiveQuicListener`, the test will fail.
      // For the case of the packets from the different addresses in the same listener wants to be
      // the same QUIC connection,(Which mentioned at
      // https://github.com/envoyproxy/envoy/issues/11184#issuecomment-679214885) it doesn't support
      // for now. When someday that case supported by envoy, we can change this testcase.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      codec_clients2.push_back(makeHttpConnection(lookupPort("address2")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10);
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_total", 16u,
                                     timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_total", 16u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (version_ == Network::Address::IpVersion::v4) {
        test_server_->waitForGaugeEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_active", i), 2u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_total", i), 2u,
            timeout_subsequent);
      } else {
        test_server_->waitForGaugeEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_active", i), 2u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_total", i), 2u,
            timeout_subsequent);
      }
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      fake_upstream_connection_ = nullptr;
      upstream_request_ = nullptr;

      auto encoder_decoder = codec_clients1[i]->startRequest(
          Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"}});
      auto& request_encoder = encoder_decoder.first;
      auto response = std::move(encoder_decoder.second);
      codec_clients1[i]->sendData(request_encoder, 1000, true);
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                       {"set-cookie", "foo"},
                                                                       {"set-cookie", "bar"}},
                                       true);

      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(response->complete());
      codec_clients1[i]->close();

      auto encoder_decoder2 = codec_clients2[i]->startRequest(
          Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"}});
      auto& request_encoder2 = encoder_decoder2.first;
      auto response2 = std::move(encoder_decoder2.second);
      codec_clients2[i]->sendData(request_encoder2, 1000, true);
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                       {"set-cookie", "foo"},
                                                                       {"set-cookie", "bar"}},
                                       true);

      ASSERT_TRUE(response2->waitForEndStream());
      EXPECT_TRUE(response2->complete());
      codec_clients2[i]->close();
    }
  }
};

class QuicHttpIntegrationSPATest
    : public QuicHttpIntegrationTestBase,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  QuicHttpIntegrationSPATest()
      : QuicHttpIntegrationTestBase(std::get<0>(GetParam()), ConfigHelper::quicHttpProxyConfig()) {}

  void SetUp() override {
    config_helper_.addRuntimeOverride(
        "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients",
        std::get<1>(GetParam()) ? "true" : "false");
  }
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(QuicHttpMultiAddressesIntegrationTest,
                         QuicHttpMultiAddressesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

static std::string SPATestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return absl::StrCat(TestUtility::ipVersionToString(std::get<0>(params.param)), "_",
                      std::get<1>(params.param) ? "all_clients_impl" : "quiche_client_impl");
}

INSTANTIATE_TEST_SUITE_P(
    QuicHttpIntegrationSPATests, QuicHttpIntegrationSPATest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(true, false)),
    SPATestParamsToString);

TEST_P(QuicHttpIntegrationTest, GetRequestAndEmptyResponse) {
  useAccessLog("%DOWNSTREAM_TLS_VERSION% %DOWNSTREAM_TLS_CIPHER% %DOWNSTREAM_TLS_SESSION_ID%");
  testRouterHeaderOnlyRequestAndResponse();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::MatchesRegex("TLSv1.3 TLS_(AES_128_GCM|CHACHA20_POLY1305)_SHA256 -"));
}

TEST_P(QuicHttpIntegrationTest, GetPeerAndLocalCertsInfo) {
  // These are not implemented yet, but configuring them shouldn't cause crash.
  useAccessLog("%DOWNSTREAM_PEER_CERT% %DOWNSTREAM_PEER_ISSUER% %DOWNSTREAM_PEER_SERIAL% "
               "%DOWNSTREAM_PEER_FINGERPRINT_1% %DOWNSTREAM_PEER_FINGERPRINT_256% "
               "%DOWNSTREAM_LOCAL_SUBJECT% %DOWNSTREAM_PEER_SUBJECT% %DOWNSTREAM_LOCAL_URI_SAN% "
               "%DOWNSTREAM_PEER_URI_SAN%");
  testRouterHeaderOnlyRequestAndResponse();
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_EQ("- - - - - - - - -", log);
}

TEST_P(QuicHttpIntegrationTest, Draft29NotSupportedByDefault) {
  supported_versions_ = {quic::ParsedQuicVersion::Draft29()};
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());
  EXPECT_EQ(quic::QUIC_INVALID_VERSION,
            static_cast<EnvoyQuicClientSession*>(codec_client_->connection())->error());
}

TEST_P(QuicHttpIntegrationTest, RuntimeEnableDraft29) {
  supported_versions_ = {quic::ParsedQuicVersion::Draft29()};
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.FLAGS_envoy_quiche_reloadable_flag_quic_disable_version_draft_29",
      "false");
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  test_server_->waitForCounterEq("http3.quic_version_h3_29", 1u);
}

TEST_P(QuicHttpIntegrationTest, CertCompressionEnabled) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.quic_support_certificate_compression", "true");
  initialize();

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"trace", "Cert compression successful"}, {"trace", "Cert decompression successful"}}),
      { testRouterHeaderOnlyRequestAndResponse(); });
}

TEST_P(QuicHttpIntegrationTest, CertCompressionDisabled) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.quic_support_certificate_compression", "false");
  initialize();

  EXPECT_LOG_NOT_CONTAINS("trace", "Cert compression successful",
                          { testRouterHeaderOnlyRequestAndResponse(); });
}

TEST_P(QuicHttpIntegrationTest, ZeroRtt) {
  // Make sure all connections use the same PersistentQuicInfoImpl.
  concurrency_ = 1;
  const Http::TestResponseHeaderMapImpl too_early_response_headers{{":status", "425"}};

  initialize();
  // Start the first connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  // Send a complete request on the first connection.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  // Close the first connection.
  codec_client_->close();

  // Start a second connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  // Send a complete request on the second connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "1"));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  // Ensure 0-RTT was used by second connection.
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_TRUE(static_cast<quic::QuicCryptoClientStream*>(
                  quic::test::QuicSessionPeer::GetMutableCryptoStream(quic_session))
                  ->EarlyDataAccepted());
  EXPECT_NE(quic_session->ssl(), nullptr);
  EXPECT_TRUE(quic_session->ssl()->peerCertificateValidated());
  // Close the second connection.
  codec_client_->close();
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq(
        "listener.127.0.0.1_0.http3.downstream.rx.quic_connection_close_error_"
        "code_QUIC_NO_ERROR",
        2u);
  } else {
    test_server_->waitForCounterEq("listener.[__1]_0.http3.downstream.rx.quic_connection_close_"
                                   "error_code_QUIC_NO_ERROR",
                                   2u);
  }

  test_server_->waitForCounterEq("http3.quic_version_rfc_v1", 2u);

  // Start the third connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  auto response3 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "1"));
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  ASSERT_TRUE(response3->waitForEndStream());
  // This is downstream sending early data, so the 425 response should be forwarded back to the
  // client.
  EXPECT_EQ("425", response3->headers().getStatusValue());
  codec_client_->close();

  // Start the fourth connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  Http::TestRequestHeaderMapImpl request{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"Early-Data", "2"}};
  auto response4 = codec_client_->makeHeaderOnlyRequest(request);
  waitForNextUpstreamRequest(0);
  // If the request already has Early-Data header, no additional Early-Data header should be added
  // and the header should be forwarded as is.
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "2"));
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  ASSERT_TRUE(response4->waitForEndStream());
  // 425 response should be forwarded back to the client.
  EXPECT_EQ("425", response4->headers().getStatusValue());
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, EarlyDataDisabled) {
  // Make sure all connections use the same PersistentQuicInfoImpl.
  concurrency_ = 1;
  enable_quic_early_data_ = false;
  initialize();
  // Start the first connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  // Send a complete request on the first connection.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  // Close the first connection.
  codec_client_->close();

  // Start a second connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Send a complete request on the second connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  // Ensure the 2nd connection is using resumption ticket but doesn't accept early data.
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_TRUE(quic_session->IsResumption());
  EXPECT_FALSE(quic_session->EarlyDataAccepted());
  // Close the second connection.
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, LegacyCertLoadingAndSelection) {
  config_helper_.addRuntimeOverride("envoy.restart_features.quic_handle_certs_with_shared_tls_code",
                                    "false");
  testMultipleQuicConnections();
}

// Not only test multiple quic connections, but disconnect and reconnect to
// trigger resumption.
TEST_P(QuicHttpIntegrationTest, MultipleUpstreamQuicConnections) {
  setUpstreamProtocol(Http::CodecType::HTTP3);
  testMultipleUpstreamQuicConnections();
}

TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsDefaultMode) {
  testMultipleQuicConnections();
}

// Ensure multiple quic connections work, regardless of platform BPF support
TEST_P(QuicHttpMultiAddressesIntegrationTest, MultipleQuicConnectionsDefaultMode) {
  testMultipleQuicConnections();
}

TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsNoBPF) {
  // Note: This setting is a no-op on platforms without BPF
  class DisableBpf {
  public:
    DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(true); }
    ~DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(false); }
  };

  DisableBpf disable;
  testMultipleQuicConnections();
}

TEST_P(QuicHttpMultiAddressesIntegrationTest, MultipleQuicConnectionsNoBPF) {
  // Note: This setting is a no-op on platforms without BPF
  class DisableBpf {
  public:
    DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(true); }
    ~DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(false); }
  };

  DisableBpf disable;
  testMultipleQuicConnections();
}

// Tests that the packets from a connection with CID longer than 8 bytes are routed to the same
// worker.
TEST_P(QuicHttpIntegrationTest, MultiWorkerWithLongConnectionId) {
  setConcurrency(8);
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // SO_REUSEPORT is needed because concurrency > 1.
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_enable_reuse_port()
        ->set_value(true);
  });

  initialize();
  // Setup 9-byte CID for the next connection.
  designated_connection_ids_.push_back(quic::test::TestConnectionIdNineBytesLong(2u));
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, PortMigration) {
  setConcurrency(2);
  initialize();
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);

  // Verify the default path degrading timeout is set correctly.
  EXPECT_EQ(4u, quic::test::QuicSentPacketManagerPeer::GetNumPtosForPathDegrading(
                    &quic_connection_->sent_packet_manager()));

  Network::Address::InstanceConstSharedPtr last_peer_addr = quic_connection_->getLastPeerAddress();
  Network::Address::InstanceConstSharedPtr last_local_addr =
      quic_connection_->getLastLocalAddress();
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);
  while (!quic_connection_->IsHandshakeConfirmed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_EQ(last_peer_addr.get(), quic_connection_->getLastPeerAddress().get());
  EXPECT_EQ(last_local_addr.get(), quic_connection_->getLastLocalAddress().get());

  // Change to a new port by switching socket, and connection should still continue.
  Network::Address::InstanceConstSharedPtr local_addr =
      Network::Test::getCanonicalLoopbackAddress(version_);
  quic_connection_->switchConnectionSocket(
      createConnectionSocket(server_addr_, local_addr, nullptr, /*prefer_gro=*/true));
  EXPECT_NE(old_port, local_addr->ip()->port());
  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));
  EXPECT_NE(last_peer_addr.get(), quic_connection_->getLastPeerAddress().get());
  EXPECT_NE(last_local_addr.get(), quic_connection_->getLastLocalAddress().get());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());

  // Switch to a socket with bad socket options.
  auto option = std::make_shared<Network::MockSocketOption>();
  EXPECT_CALL(*option, setOption(_, _))
      .WillRepeatedly(
          Invoke([](Network::Socket&, envoy::config::core::v3::SocketOption::SocketState state) {
            if (state == envoy::config::core::v3::SocketOption::STATE_LISTENING) {
              return false;
            }
            return true;
          }));
  auto options = std::make_shared<Network::Socket::Options>();
  options->push_back(option);
  quic_connection_->switchConnectionSocket(
      createConnectionSocket(server_addr_, local_addr, options, /*prefer_gro=*/true));
  EXPECT_TRUE(codec_client_->disconnected());
  cleanupUpstreamAndDownstream();
}

TEST_P(QuicHttpIntegrationTest, PortMigrationOnPathDegrading) {
  setConcurrency(2);
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(2);
  uint32_t old_port = lookupPort("http");
  auto options = std::make_shared<Network::Socket::Options>();
  auto option = std::make_shared<Network::MockSocketOption>();
  options->push_back(option);
  EXPECT_CALL(*option, setOption(_, _)).Times(3u);
  codec_client_ = makeHttpConnection(makeClientConnectionWithOptions(old_port, options));

  // Make sure that the port migration config is plumbed through.
  EXPECT_EQ(2u, quic::test::QuicSentPacketManagerPeer::GetNumPtosForPathDegrading(
                    &quic_connection_->sent_packet_manager()));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());

  for (uint8_t i = 0; i < 5; i++) {
    auto old_self_addr = quic_connection_->self_address();
    EXPECT_CALL(*option, setOption(_, _)).Times(3u);
    quic_connection_->OnPathDegradingDetected();
    ASSERT_TRUE(quic_connection_->waitForPathResponse());
    auto self_addr = quic_connection_->self_address();
    EXPECT_NE(old_self_addr, self_addr);
    ASSERT_TRUE(quic_connection_->waitForNewCid());
  }

  // port migration is disabled once socket switch limit is reached.
  EXPECT_CALL(*option, setOption(_, _)).Times(0);
  quic_connection_->OnPathDegradingDetected();

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, NoPortMigrationWithoutConfig) {
  setConcurrency(2);
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(0);
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  auto old_self_addr = quic_connection_->self_address();
  quic_connection_->OnPathDegradingDetected();
  ASSERT_FALSE(quic_connection_->waitForPathResponse(std::chrono::milliseconds(2000)));
  auto self_addr = quic_connection_->self_address();
  EXPECT_EQ(old_self_addr, self_addr);

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, PortMigrationFailureOnPathDegrading) {
  setConcurrency(2);
  validation_failure_on_path_response_ = true;
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(2);
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  auto old_self_addr = quic_connection_->self_address();
  quic_connection_->OnPathDegradingDetected();
  ASSERT_TRUE(quic_connection_->waitForPathResponse());
  auto self_addr = quic_connection_->self_address();
  // The path validation will fail and thus client self address will not change.
  EXPECT_EQ(old_self_addr, self_addr);

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, AdminDrainDrainsListeners) {
  testAdminDrain(Http::CodecType::HTTP1);
}

TEST_P(QuicHttpIntegrationTest, CertVerificationFailure) {
  san_to_match_ = "www.random_domain.com";
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
  std::string failure_reason = "QUIC_TLS_CERTIFICATE_UNKNOWN with details: TLS handshake failure "
                               "(ENCRYPTION_HANDSHAKE) 46: "
                               "certificate unknown. SSLErrorStack:";
  EXPECT_THAT(codec_client_->connection()->transportFailureReason(),
              testing::HasSubstr(failure_reason));
}

TEST_P(QuicHttpIntegrationTest, ResetRequestWithoutAuthorityHeader) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/dynamo/url"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForReset());
  ASSERT_FALSE(response->complete());
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, ResetRequestWithInvalidCharacter) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string value = std::string(1, 2);
  EXPECT_FALSE(Http::HeaderUtility::headerValueIsValid(value));
  default_request_headers_.addCopy("illegal_header", value);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());

  // Verify stream error counters are correctly incremented.
  std::string counter_scope = GetParam() == Network::Address::IpVersion::v4
                                  ? "listener.127.0.0.1_0.http3.downstream.tx."
                                  : "listener.[__1]_0.http3.downstream.tx.";
  std::string error_code = "quic_connection_close_error_code_QUIC_HTTP_FRAME_ERROR";
  test_server_->waitForCounterEq(absl::StrCat(counter_scope, error_code), 1U);
}

TEST_P(QuicHttpIntegrationTest, Http3ClientKeepalive) {
  initialize();

  constexpr uint64_t max_interval_sec = 5;
  constexpr uint64_t initial_interval_sec = 1;
  // Set connection idle network timeout to be a little larger than max interval.
  dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
      .quic_config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromSeconds(max_interval_sec + 2));
  client_quic_options_.mutable_connection_keepalive()->mutable_max_interval()->set_seconds(
      max_interval_sec);
  client_quic_options_.mutable_connection_keepalive()->mutable_initial_interval()->set_seconds(
      initial_interval_sec);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Wait for 10s before sending back response. If keepalive is disabled, the
  // connection would have idle timed out.
  Event::TimerPtr timer(dispatcher_->createTimer([this]() -> void { dispatcher_->exit(); }));
  timer->enableTimer(std::chrono::seconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                   {"set-cookie", "foo"},
                                                                   {"set-cookie", "bar"}},
                                   true);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // First 6 PING frames should be sent every 1s, and the following ones less frequently.
  EXPECT_LE(quic_connection_->GetStats().ping_frames_sent, 8u);
}

TEST_P(QuicHttpIntegrationTest, Http3ClientKeepaliveDisabled) {
  initialize();

  constexpr uint64_t max_interval_sec = 0;
  constexpr uint64_t initial_interval_sec = 1;
  // Set connection idle network timeout to be a little larger than max interval.
  dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
      .quic_config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromSeconds(5));
  client_quic_options_.mutable_connection_keepalive()->mutable_max_interval()->set_seconds(
      max_interval_sec);
  client_quic_options_.mutable_connection_keepalive()->mutable_initial_interval()->set_seconds(
      initial_interval_sec);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // As keepalive is disabled, the connection will timeout after 5s.
  EXPECT_TRUE(response->waitForReset());
  EXPECT_EQ(quic_connection_->GetStats().ping_frames_sent, 0u);
}

TEST_P(QuicHttpIntegrationTest, Http3DownstreamKeepalive) {
  constexpr uint64_t max_interval_sec = 5;
  constexpr uint64_t initial_interval_sec = 1;
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* keepalive_options = hcm.mutable_http3_protocol_options()
                                      ->mutable_quic_protocol_options()
                                      ->mutable_connection_keepalive();
        keepalive_options->mutable_initial_interval()->set_seconds(initial_interval_sec);
        keepalive_options->mutable_max_interval()->set_seconds(max_interval_sec);
      });
  // Set connection idle network timeout to be a little larger than max interval.
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_udp_listener_config()
        ->mutable_quic_options()
        ->mutable_idle_timeout()
        ->set_seconds(max_interval_sec + 2);
  });
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Wait for 10s before sending back response. If keepalive is disabled, the
  // connection would have idle timed out.
  Event::TimerPtr timer(dispatcher_->createTimer([this]() -> void { dispatcher_->exit(); }));
  timer->enableTimer(std::chrono::seconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                   {"set-cookie", "foo"},
                                                                   {"set-cookie", "bar"}},
                                   true);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(QuicHttpIntegrationTest, NoStreams) {
  // Tighten the stream idle timeout, as it defaults to 5m
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        hcm.mutable_stream_idle_timeout()->set_nanos(400 * 1000 * 1000);
      });

  // Set the fake upstream to start with 0 streams available.
  setUpstreamProtocol(Http::CodecType::HTTP3);
  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(0);
  mergeOptions(options);
  initialize();

  // Create the client connection and send a request.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Make sure the time out closes the stream.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
}

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationSucceeds) {
  // Config the client to defer cert validation by 5ms.
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config.get());
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->connected());
}

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationAfterDisconnect) {
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config.get());

  // Change the configured cert validation to defer 1s.
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setExpectedPeerAddress(fmt::format(
          "{}:{}", Network::Test::getLoopbackAddressUrlString(version_), lookupPort("http")));
  // Change the handshake timeout to be 500ms to fail the handshake while the cert validation is
  // pending.
  quic::QuicTime::Delta connect_timeout = quic::QuicTime::Delta::FromMilliseconds(500);
  auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
  persistent_info.quic_config_.set_max_idle_time_before_crypto_handshake(connect_timeout);
  persistent_info.quic_config_.set_max_time_before_crypto_handshake(connect_timeout);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx = transport_socket_factory_->sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationAfterTearDown) {
  auto custom_validator_config = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(
      envoy::config::core::v3::TypedExtensionConfig());
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config.get());
  // Change the configured cert validation to defer 1s.
  auto cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  // Change the handshake timeout to be 500ms to fail the handshake while the cert validation is
  // pending.
  quic::QuicTime::Delta connect_timeout = quic::QuicTime::Delta::FromMilliseconds(500);
  auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
  persistent_info.quic_config_.set_max_idle_time_before_crypto_handshake(connect_timeout);
  persistent_info.quic_config_.set_max_time_before_crypto_handshake(connect_timeout);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx = transport_socket_factory_->sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  codec_client_.reset();
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_P(QuicHttpIntegrationTest, MultipleNetworkFilters) {
  config_helper_.addNetworkFilter(R"EOF(
      name: envoy.test.test_network_filter
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.TestNetworkFilterConfig
)EOF");
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  test_server_->waitForCounterEq("test_network_filter.on_new_connection", 1);
  EXPECT_EQ(test_server_->counter("test_network_filter.on_data")->value(), 0);
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, DeferredLogging) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();

  // Make a header-only request and delay the response by 1ms to ensure that the ROUNDTRIP_DURATION
  // metric is > 0 if exists.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  absl::SleepFor(absl::Milliseconds(1));
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  RELEASE_ASSERT(response->waitForEndStream(TestUtility::DefaultTimeout), "unexpected timeout");
  codec_client_->close();

  std::string log = waitForAccessLog(access_log_name_);

  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_GT(/* ROUNDTRIP_DURATION */ std::stoi(metrics.at(1)), 0);
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(metrics.at(3)), 0);
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "200");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  // Ensure that request headers from top-level access logger parameter and stream info are
  // consistent.
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithBlackholedClient) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();

  // Make a header-only request and delay the response by 1ms to ensure that the ROUNDTRIP_DURATION
  // metric is > 0 if exists.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  absl::SleepFor(absl::Milliseconds(1));
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Prevent the client's dispatcher from running by not calling waitForEndStream or closing the
  // client's connection, then wait for server to tear down connection due to too many
  // retransmissions.
  int iterations = 0;
  std::string contents = TestEnvironment::readFileToStringForTest(access_log_name_);
  while (iterations < 20) {
    timeSystem().advanceTimeWait(std::chrono::seconds(1));
    // check for deferred logs from connection teardown.
    contents = TestEnvironment::readFileToStringForTest(access_log_name_);
    if (!contents.empty()) {
      break;
    }
    iterations++;
  }

  std::vector<std::string> entries = absl::StrSplit(contents, '\n', absl::SkipEmpty());
  EXPECT_EQ(entries.size(), 1);
  std::string log = entries[0];

  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(metrics.at(3)), 0);
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "200");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  // Ensure that request headers from top-level access logger parameter and stream info are
  // consistent.
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));

  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingDisabled) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "false");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  sendRequestAndWaitForResponse(default_request_headers_, /*request_size=*/0,
                                default_response_headers_,
                                /*response_size=*/0,
                                /*upstream_index=*/0, TestUtility::DefaultTimeout);
  codec_client_->close();

  // Do not flush client acks.
  std::string log = waitForAccessLog(access_log_name_, 0, false, nullptr);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(metrics.at(3)), 0);
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "200");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithReset) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  codec_client_->close();
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());

  std::string log = waitForAccessLog(access_log_name_);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_EQ(/* RESPONSE_DURATION */ metrics.at(3), "-");
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "0");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithQuicReset) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // omit required authority header to invoke EnvoyQuicServerStream::onStreamError
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/dynamo/url"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());

  std::string log = waitForAccessLog(access_log_name_);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_EQ(/* REQUEST_DURATION */ metrics.at(2), "-");
  EXPECT_EQ(/* RESPONSE_DURATION */ metrics.at(3), "-");
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "0");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithEnvoyReset) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.FLAGS_envoy_quiche_reloadable_flag_quic_act_upon_invalid_header",
      "false");

  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // omit required authority header to invoke EnvoyQuicServerStream::resetStream
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/dynamo/url"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  ASSERT_TRUE(response->complete());

  std::string log = waitForAccessLog(access_log_name_);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 21);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_EQ(/* REQUEST_DURATION */ metrics.at(2), "-");
  EXPECT_EQ(/* RESPONSE_DURATION */ metrics.at(3), "-");
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "400");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithInternalRedirect) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog(
      "%PROTOCOL%,%ROUNDTRIP_DURATION%,%REQUEST_DURATION%,%RESPONSE_DURATION%,%RESPONSE_"
      "CODE%,%BYTES_RECEIVED%,%ROUTE_NAME%,%VIRTUAL_CLUSTER_NAME%,%RESPONSE_CODE_DETAILS%,%"
      "CONNECTION_TERMINATION_DETAILS%,%START_TIME%,%UPSTREAM_HOST%,%DURATION%,%BYTES_SENT%,%"
      "RESPONSE_FLAGS%,%DOWNSTREAM_LOCAL_ADDRESS%,%UPSTREAM_CLUSTER%,%STREAM_ID%,%DYNAMIC_"
      "METADATA("
      "udp.proxy.session:bytes_sent)%,%REQ(:path)%,%STREAM_INFO_REQ(:path)%,%RESP(test-header)%");
  auto handle = config_helper_.createVirtualHost("handle.internal.redirect");
  handle.mutable_routes(0)->set_name("redirect");
  handle.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy();
  config_helper_.addVirtualHost(handle);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  Http::TestResponseHeaderMapImpl redirect_response{{":status", "302"},
                                                    {"content-length", "0"},
                                                    {"location", "http://authority2/new/url"},
                                                    // Test header added to confirm that response
                                                    // headers are populated for internal redirects
                                                    {"test-header", "test-header-value"}};

  upstream_request_->encodeHeaders(redirect_response, true);
  std::string log = waitForAccessLog(access_log_name_, 0);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 22);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  // no roundtrip duration for internal redirect.
  EXPECT_EQ(/* ROUNDTRIP_DURATION */ metrics.at(1), "-");
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(metrics.at(3)), 0);
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "302");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
  EXPECT_EQ(/* RESPONSE_CODE_DETAILS */ metrics.at(8), "internal_redirect");
  EXPECT_EQ(/* RESP(test-header) */ metrics.at(21), "test-header-value");

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());

  log = waitForAccessLog(access_log_name_, 1);
  metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 22);
  EXPECT_EQ(/* PROTOCOL */ metrics.at(0), "HTTP/3");
  // roundtrip duration populated on final log.
  EXPECT_GT(/* ROUNDTRIP_DURATION */ std::stoi(metrics.at(1)), 0);
  EXPECT_GE(/* REQUEST_DURATION */ std::stoi(metrics.at(2)), 0);
  EXPECT_GE(/* RESPONSE_DURATION */ std::stoi(metrics.at(3)), 0);
  EXPECT_EQ(/* RESPONSE_CODE */ metrics.at(4), "200");
  EXPECT_EQ(/* BYTES_RECEIVED */ metrics.at(5), "0");
  EXPECT_EQ(/* request headers */ metrics.at(19), metrics.at(20));
  EXPECT_EQ(/* RESPONSE_CODE_DETAILS */ metrics.at(8), "via_upstream");
  // no test header
  EXPECT_EQ(/* RESP(test-header) */ metrics.at(21), "-");
}

TEST_P(QuicHttpIntegrationTest, DeferredLoggingWithRetransmission) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_defer_logging_to_ack_listener",
                                    "true");
  useAccessLog("%BYTES_RETRANSMITTED%,%PACKETS_RETRANSMITTED%");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);

  // Temporarily prevent server from writing packets (i.e. to respond to downstream)
  // to simulate packet loss and trigger retransmissions.
  {
    SocketInterfaceSwap socket_swap(downstreamProtocol() == Http::CodecType::HTTP3
                                        ? Network::Socket::Type::Datagram
                                        : Network::Socket::Type::Stream);
    Api::IoErrorPtr ebadf = Network::IoSocketError::getIoSocketEbadfError();
    socket_swap.write_matcher_->setDestinationPort(lookupPort("http"));
    socket_swap.write_matcher_->setWriteOverride(std::move(ebadf));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    timeSystem().advanceTimeWait(std::chrono::seconds(TIMEOUT_FACTOR));
  }

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  ASSERT_TRUE(response->complete());

  // Confirm that retransmissions are logged.
  std::string log = waitForAccessLog(access_log_name_);
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 2);
  EXPECT_GT(/* BYTES_RETRANSMITTED */ std::stoi(metrics.at(0)), 0);
  EXPECT_GT(/* PACKETS_RETRANSMITTED */ std::stoi(metrics.at(1)), 0);
  EXPECT_GE(std::stoi(metrics.at(0)), std::stoi(metrics.at(1)));
}

TEST_P(QuicHttpIntegrationTest, InvalidTrailer) {
  initialize();
  // Empty string in trailer key is invalid.
  Http::TestRequestTrailerMapImpl request_trailers{{"", "foo"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, 1024, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  // Request fails due to invalid trailer.
  ASSERT_TRUE(response->waitForReset());
  EXPECT_FALSE(response->complete());
}

TEST_P(QuicHttpIntegrationTest, AlpnProtocolMismatch) {
  client_alpn_ = "h3-special";
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());
  EXPECT_EQ(quic::QUIC_HANDSHAKE_FAILED,
            static_cast<EnvoyQuicClientSession*>(codec_client_->connection())->error());
  EXPECT_THAT(codec_client_->connection()->transportFailureReason(),
              testing::HasSubstr("no application protocol"));
}

TEST_P(QuicHttpIntegrationTest, ConfigureAlpnProtocols) {
  client_alpn_ = "h3-special";
  custom_alpns_ = {"h3", "h3-special"};
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
}

class QuicInplaceLdsIntegrationTest : public QuicHttpIntegrationTest {
public:
  void inplaceInitialize(bool add_default_filter_chain = false) {
    autonomous_upstream_ = true;
    setUpstreamCount(2);

    config_helper_.addConfigModifier([add_default_filter_chain](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain_0 =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      *filter_chain_0->mutable_filter_chain_match()->mutable_server_names()->Add() = "www.lyft.com";
      auto* filter_chain_1 = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains()
                                 ->Add();
      filter_chain_1->MergeFrom(*filter_chain_0);

      // filter chain 1 route to cluster_1
      *filter_chain_1->mutable_filter_chain_match()->mutable_server_names(0) = "lyft.com";

      filter_chain_0->set_name("filter_chain_0");
      filter_chain_1->set_name("filter_chain_1");

      auto* config_blob = filter_chain_1->mutable_filters(0)->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::http_connection_manager::v3::
                                      HttpConnectionManager>());
      auto hcm_config = MessageUtil::anyConvert<
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
          *config_blob);
      hcm_config.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("cluster_1");
      config_blob->PackFrom(hcm_config);
      bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
          *bootstrap.mutable_static_resources()->mutable_clusters(0));
      bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");

      if (add_default_filter_chain) {
        auto default_filter_chain = bootstrap.mutable_static_resources()
                                        ->mutable_listeners(0)
                                        ->mutable_default_filter_chain();
        default_filter_chain->MergeFrom(*filter_chain_0);
        default_filter_chain->set_name("filter_chain_default");
      }
    });

    QuicHttpIntegrationTest::initialize();
  }

  void makeRequestAndWaitForResponse(IntegrationCodecClient& codec_client) {
    IntegrationStreamDecoderPtr response =
        codec_client.makeHeaderOnlyRequest(default_request_headers_);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_FALSE(codec_client.sawGoAway());
  }
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicInplaceLdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicInplaceLdsIntegrationTest, ReloadConfigUpdateNonDefaultFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client_0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  auto codec_client_1 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));

  // Remove filter_chain_1.
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  makeRequestAndWaitForResponse(*codec_client_0);
  EXPECT_TRUE(codec_client_1->sawGoAway());
  codec_client_1->close();

  auto codec_client_2 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_2);
  codec_client_2->close();

  // Update filter chain again to add back filter_chain_1.
  config_helper_.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 2);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 3);

  auto codec_client_3 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_3);
  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();
  codec_client_3->close();
}

// Verify that the connection received GO_AWAY after its filter chain gets deleted during the
// listener update.
TEST_P(QuicInplaceLdsIntegrationTest, ReloadConfigUpdateDefaultFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/true);

  auto codec_client_0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));

  // Remove filter_chain_1.
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);

  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  // This connection should pick up the default filter chain.
  auto codec_client_default =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_default);
  makeRequestAndWaitForResponse(*codec_client_0);

  // Modify the default filter chain.
  ConfigHelper new_config_helper1(version_, new_config_helper.bootstrap());
  new_config_helper1.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                           -> void {
    auto default_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_default_filter_chain();
    default_filter_chain->set_name("default_filter_chain_v3");
  });

  new_config_helper1.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 2);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  makeRequestAndWaitForResponse(*codec_client_0);
  EXPECT_TRUE(codec_client_default->sawGoAway());
  codec_client_default->close();

  // This connection should pick up the new default filter chain.
  auto codec_client_1 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_1);

  // Remove the default filter chain.
  ConfigHelper new_config_helper2(version_, config_helper_.bootstrap());
  new_config_helper2.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->clear_default_filter_chain();
      });

  new_config_helper2.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 3);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();
  EXPECT_TRUE(codec_client_1->sawGoAway());
  codec_client_1->close();
}

TEST_P(QuicInplaceLdsIntegrationTest, EnableAndDisableEarlyData) {
  enable_quic_early_data_ = true;
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client_0 = makeRawHttp3Connection(
      makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"), absl::nullopt,
      /*wait_for_1rtt_key*/ true);
  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();

  // Modify 1st transport socket factory to disable early data.
  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addQuicDownstreamTransportSocketConfig(/*enable_early_data=*/false, {});

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);

  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  // The 2nd connection should try to do 0-RTT but get rejected and QUICHE will transparently retry
  // the request after handshake completes.
  auto codec_client_2 = makeRawHttp3Connection(
      makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"), absl::nullopt,
      /*wait_for_1rtt_key*/ false);
  makeRequestAndWaitForResponse(*codec_client_2);

  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_2->connection());
  EXPECT_FALSE(quic_session->EarlyDataAccepted());
  codec_client_2->close();
}

TEST_P(QuicInplaceLdsIntegrationTest, StatelessResetOldConnection) {
  enable_quic_early_data_ = true;
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  makeRequestAndWaitForResponse(*codec_client0);
  // Make the next connection use the same connection ID.
  designated_connection_ids_.push_back(quic_connection_->connection_id());
  codec_client0->close();
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForGaugeEq("listener.127.0.0.1_0.downstream_cx_active", 0u);
  } else {
    test_server_->waitForGaugeEq("listener.[__1]_0.downstream_cx_active", 0u);
  }

  // This new connection would be reset.
  auto codec_client1 =
      makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client1->disconnected());

  quic::QuicErrorCode error =
      static_cast<EnvoyQuicClientSession*>(codec_client1->connection())->error();
  EXPECT_TRUE(error == quic::QUIC_NETWORK_IDLE_TIMEOUT || error == quic::QUIC_HANDSHAKE_TIMEOUT);
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterGe(
        "listener.127.0.0.1_0.quic.dispatcher.stateless_reset_packets_sent", 1u);
  } else {
    test_server_->waitForCounterGe("listener.[__1]_0.quic.dispatcher.stateless_reset_packets_sent",
                                   1u);
  }
}

TEST_P(QuicHttpIntegrationSPATest, UsesPreferredAddress) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [=, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listen_address = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_address()
                                   ->mutable_socket_address();
        // Change listening address to Any.
        listen_address->set_address(version_ == Network::Address::IpVersion::v4 ? "0.0.0.0" : "::");
        auto* preferred_address_config = bootstrap.mutable_static_resources()
                                             ->mutable_listeners(0)
                                             ->mutable_udp_listener_config()
                                             ->mutable_quic_options()
                                             ->mutable_server_preferred_address_config();
        // Configure a loopback interface as the server's preferred address.
        preferred_address_config->set_name("quic.server_preferred_address.fixed");
        envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig
            server_preferred_address;
        server_preferred_address.set_ipv4_address("127.0.0.2");
        server_preferred_address.set_ipv6_address("::2");
        preferred_address_config->mutable_typed_config()->PackFrom(server_preferred_address);

        // Configure a test listener filter which is incompatible with any server preferred
        // addresses but with any matcher, which effectively disables the filter.
        auto* listener_filter =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
        listener_filter->set_name("dumb_filter");
        auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
        configuration.set_added_value("foo");
        configuration.set_allow_server_migration(false);
        configuration.set_allow_client_migration(false);
        listener_filter->mutable_typed_config()->PackFrom(configuration);
        listener_filter->mutable_filter_disabled()->set_any_match(true);
      });

  initialize();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients")) {
    quic::QuicTagVector connection_options{quic::kSPAD};
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetConnectionOptionsToSend(connection_options);
  }
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE((version_ == Network::Address::IpVersion::v4 &&
               quic_session->config()->HasReceivedIPv4AlternateServerAddress()) ||
              (version_ == Network::Address::IpVersion::v6 &&
               quic_session->config()->HasReceivedIPv6AlternateServerAddress()));
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  EXPECT_TRUE(quic_connection_->IsValidatingServerPreferredAddress());
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  if (version_ == Network::Address::IpVersion::v4) {
    // Most v6 platform doesn't support two loopback interfaces.
    EXPECT_EQ("127.0.0.2", quic_connection_->peer_address().host().ToString());
    test_server_->waitForCounterGe(
        "listener.0.0.0.0_0.quic.connection.num_packets_rx_on_preferred_address", 2u);
  }
}

TEST_P(QuicHttpIntegrationSPATest, UsesPreferredAddressDNAT) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [=, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listen_address = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_address()
                                   ->mutable_socket_address();
        // Change listening address to Any.
        listen_address->set_address(Network::Test::getAnyAddressString(version_));
        auto* preferred_address_config = bootstrap.mutable_static_resources()
                                             ->mutable_listeners(0)
                                             ->mutable_udp_listener_config()
                                             ->mutable_quic_options()
                                             ->mutable_server_preferred_address_config();

        // Configure a loopback interface as the server's preferred address.
        preferred_address_config->set_name("quic.server_preferred_address.fixed");
        envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig
            server_preferred_address;
        server_preferred_address.mutable_ipv4_config()->mutable_address()->set_address("1.2.3.4");
        server_preferred_address.mutable_ipv4_config()->mutable_address()->set_port_value(12345);
        server_preferred_address.mutable_ipv4_config()->mutable_dnat_address()->set_address(
            "127.0.0.2");
        server_preferred_address.mutable_ipv4_config()->mutable_dnat_address()->set_port_value(0);

        server_preferred_address.mutable_ipv6_config()->mutable_address()->set_address("::1");
        server_preferred_address.mutable_ipv6_config()->mutable_address()->set_port_value(12345);
        server_preferred_address.mutable_ipv6_config()->mutable_dnat_address()->set_address("::2");
        server_preferred_address.mutable_ipv6_config()->mutable_dnat_address()->set_port_value(0);
        preferred_address_config->mutable_typed_config()->PackFrom(server_preferred_address);

        // Configure a test listener filter which is incompatible with any server preferred
        // addresses but with any matcher, which effectively disables the filter.
        auto* listener_filter =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
        listener_filter->set_name("dumb_filter");
        auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
        configuration.set_added_value("foo");
        configuration.set_allow_server_migration(false);
        configuration.set_allow_client_migration(false);
        listener_filter->mutable_typed_config()->PackFrom(configuration);
        listener_filter->mutable_filter_disabled()->set_any_match(true);
      });

  // Do socket swap before initialization to avoid races.
  SocketInterfaceSwap socket_swap(Network::Socket::Type::Datagram);

  initialize();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients")) {
    quic::QuicTagVector connection_options{quic::kSPAD};
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetConnectionOptionsToSend(connection_options);
  }
  auto listener_port = lookupPort("http");

  // Setup DNAT for 1.2.3.4:12345-->127.0.0.2:listener_port
  socket_swap.write_matcher_->setDnat(
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 12345),
      Network::Utility::parseInternetAddressNoThrow("127.0.0.2", listener_port));

  codec_client_ = makeHttpConnection(makeClientConnection(listener_port));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE((version_ == Network::Address::IpVersion::v4 &&
               quic_session->config()->HasReceivedIPv4AlternateServerAddress()) ||
              (version_ == Network::Address::IpVersion::v6 &&
               quic_session->config()->HasReceivedIPv6AlternateServerAddress()));
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  EXPECT_TRUE(quic_connection_->IsValidatingServerPreferredAddress());
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  if (version_ == Network::Address::IpVersion::v4) {
    // Most v6 platform doesn't support two loopback interfaces.
    EXPECT_EQ("1.2.3.4", quic_connection_->peer_address().host().ToString());
    test_server_->waitForCounterGe(
        "listener.0.0.0.0_0.quic.connection.num_packets_rx_on_preferred_address", 2u);
  }

  // Close connections before `SocketInterfaceSwap` goes out of scope to ensure packets aren't
  // processed while it is being swapped back.
  cleanupUpstreamAndDownstream();
}

TEST_P(QuicHttpIntegrationSPATest, PreferredAddressRuntimeFlag) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients")) {
    return;
  }
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier(
      [=, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listen_address = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_address()
                                   ->mutable_socket_address();
        // Change listening address to Any.
        listen_address->set_address(version_ == Network::Address::IpVersion::v4 ? "0.0.0.0" : "::");
        auto* preferred_address_config = bootstrap.mutable_static_resources()
                                             ->mutable_listeners(0)
                                             ->mutable_udp_listener_config()
                                             ->mutable_quic_options()
                                             ->mutable_server_preferred_address_config();
        // Configure a loopback interface as the server's preferred address.
        preferred_address_config->set_name("quic.server_preferred_address.fixed");
        envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig
            server_preferred_address;
        server_preferred_address.set_ipv4_address("127.0.0.2");
        server_preferred_address.set_ipv6_address("::2");
        preferred_address_config->mutable_typed_config()->PackFrom(server_preferred_address);

        // Configure a test listener filter which is incompatible with any server preferred
        // addresses but with any matcher, which effectively disables the filter.
        auto* listener_filter =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
        listener_filter->set_name("dumb_filter");
        auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
        configuration.set_added_value("foo");
        configuration.set_allow_server_migration(false);
        configuration.set_allow_client_migration(false);
        listener_filter->mutable_typed_config()->PackFrom(configuration);
        listener_filter->mutable_filter_disabled()->set_any_match(true);
      });

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  EXPECT_TRUE(!quic_session->config()->HasReceivedIPv4AlternateServerAddress() &&
              !quic_session->config()->HasReceivedIPv6AlternateServerAddress());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  EXPECT_FALSE(quic_connection_->IsValidatingServerPreferredAddress());
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(QuicHttpIntegrationSPATest, UsesPreferredAddressDualStack) {
  if (!(TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6) &&
        version_ == Network::Address::IpVersion::v4)) {
    return;
  }
  // Only run this test with a v4 client if the test environment supports dual stack socket.
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listen_address = bootstrap.mutable_static_resources()
                               ->mutable_listeners(0)
                               ->mutable_address()
                               ->mutable_socket_address();
    // Change listening address to Any. As long as the test environment support IPv6 use [::] as
    // listening address.
    listen_address->set_address("::");
    listen_address->set_ipv4_compat(true);
    auto* preferred_address_config = bootstrap.mutable_static_resources()
                                         ->mutable_listeners(0)
                                         ->mutable_udp_listener_config()
                                         ->mutable_quic_options()
                                         ->mutable_server_preferred_address_config();
    // Configure a loopback interface as the server's preferred address.
    preferred_address_config->set_name("quic.server_preferred_address.fixed");
    envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig
        server_preferred_address;
    server_preferred_address.set_ipv4_address("127.0.0.2");
    preferred_address_config->mutable_typed_config()->PackFrom(server_preferred_address);
  });

  initialize();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients")) {
    quic::QuicTagVector connection_options{quic::kSPAD};
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetConnectionOptionsToSend(connection_options);
  }

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE(quic_session->config()->HasReceivedIPv4AlternateServerAddress());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  EXPECT_TRUE(quic_connection_->IsValidatingServerPreferredAddress());
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("127.0.0.2", quic_connection_->peer_address().host().ToString());
  test_server_->waitForCounterGe(
      "listener.[__]_0.quic.connection.num_packets_rx_on_preferred_address", 2u);
}

TEST_P(QuicHttpIntegrationTest, PreferredAddressDroppedByIncompatibleListenerFilter) {
  autonomous_upstream_ = true;
  useAccessLog(fmt::format("%RESPONSE_CODE% %FILTER_STATE({})%",
                           TestQuicListenerFilter::TestStringFilterState::key()));
  config_helper_.addConfigModifier(
      [=, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listen_address = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_address()
                                   ->mutable_socket_address();
        // Change listening address to Any.
        listen_address->set_address(version_ == Network::Address::IpVersion::v4 ? "0.0.0.0" : "::");
        auto* preferred_address_config = bootstrap.mutable_static_resources()
                                             ->mutable_listeners(0)
                                             ->mutable_udp_listener_config()
                                             ->mutable_quic_options()
                                             ->mutable_server_preferred_address_config();
        // Configure a loopback interface as the server's preferred address.
        preferred_address_config->set_name("quic.server_preferred_address.fixed");
        envoy::extensions::quic::server_preferred_address::v3::FixedServerPreferredAddressConfig
            server_preferred_address;
        server_preferred_address.set_ipv4_address("127.0.0.2");
        server_preferred_address.set_ipv6_address("::2");
        preferred_address_config->mutable_typed_config()->PackFrom(server_preferred_address);

        // Configure a test listener filter which is incompatible with any server preferred
        // addresses.
        auto* listener_filter =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
        listener_filter->set_name("dumb_filter");
        auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
        configuration.set_added_value("foo");
        configuration.set_allow_server_migration(false);
        configuration.set_allow_client_migration(false);
        listener_filter->mutable_typed_config()->PackFrom(configuration);
      });

  initialize();
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients")) {
    quic::QuicTagVector connection_options{quic::kSPAD};
    dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
        .quic_config_.SetConnectionOptionsToSend(connection_options);
  }
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  EXPECT_TRUE(!quic_session->config()->HasReceivedIPv4AlternateServerAddress() &&
              !quic_session->config()->HasReceivedIPv6AlternateServerAddress());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  EXPECT_FALSE(quic_connection_->IsValidatingServerPreferredAddress());
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  std::string log = waitForAccessLog(access_log_name_, 0);
  EXPECT_THAT(log, testing::HasSubstr("200 \"foo\""));
}

// Validate that the correct transport parameter is sent when `send_disable_active_migration` is
// enabled.
TEST_P(QuicHttpIntegrationTest, SendDisableActiveMigration) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_udp_listener_config()
        ->mutable_quic_options()
        ->mutable_send_disable_active_migration()
        ->set_value(true);
  });

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());

  // Validate the setting was transmitted.
  EXPECT_TRUE(quic_session->config()->DisableConnectionMigration());

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(QuicHttpIntegrationTest, RejectTraffic) {
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_udp_listener_config()
        ->mutable_quic_options()
        ->set_reject_new_connections(true);
  });

  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());
  EXPECT_EQ(quic::QUIC_INVALID_VERSION,
            static_cast<EnvoyQuicClientSession*>(codec_client_->connection())->error());
}

// Validate that the transport parameter is not sent when `send_disable_active_migration` is
// unset.
TEST_P(QuicHttpIntegrationTest, UnsetSendDisableActiveMigration) {
  autonomous_upstream_ = true;

  // No config modifier to enable the setting.

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());

  // Validate the setting was not transmitted.
  EXPECT_FALSE(quic_session->config()->DisableConnectionMigration());

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},
      {":path", "/test/long/url"},
      {":authority", "sni.lyft.com"},
      {":scheme", "http"},
      {AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1024 * 1024)}};
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

// Validate that debug visitors are attached to connections when configured.
TEST_P(QuicHttpIntegrationTest, ConnectionDebugVisitor) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto debug_visitor_config = bootstrap.mutable_static_resources()
                                    ->mutable_listeners(0)
                                    ->mutable_udp_listener_config()
                                    ->mutable_quic_options()
                                    ->mutable_connection_debug_visitor_config();
    debug_visitor_config->set_name("envoy.quic.connection_debug_visitor.basic");
    envoy::extensions::quic::connection_debug_visitor::v3::BasicConfig config;
    debug_visitor_config->mutable_typed_config()->PackFrom(config);
  });

  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  EXPECT_EQ(Network::Test::getLoopbackAddressString(version_),
            quic_connection_->peer_address().host().ToString());
  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  std::string listener = version_ == Network::Address::IpVersion::v4 ? "127.0.0.1_0" : "[__1]_0";
  WAIT_FOR_LOG_CONTAINS(
      "info",
      fmt::format("Quic connection from {} with id {} closed {} with details:",
                  quic_connection_->self_address().ToString(),
                  quic_connection_->connection_id().ToString(),
                  quic::ConnectionCloseSourceToString(quic::ConnectionCloseSource::FROM_PEER)),
      {
        quic_session->close(Network::ConnectionCloseType::NoFlush);
        test_server_->waitForGaugeEq(fmt::format("listener.{}.downstream_cx_active", listener), 0u);
      });
}

TEST_P(QuicHttpIntegrationTest, StreamTimeoutWithHalfClose) {
  // Tighten the stream idle timeout to 400ms.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        hcm.mutable_stream_idle_timeout()->set_nanos(400 * 1000 * 1000);
      });
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, "partial body", false);
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  quic::QuicStream* stream = quic_session->GetActiveStream(0);
  // Only send RESET_STREAM to close write side of this stream.
  stream->ResetWriteSide(quic::QuicResetStreamError::FromInternal(quic::QUIC_STREAM_NO_ERROR));

  // Wait for the server to timeout this request and the local reply.
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_idle_timeout")->value());
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, QuicListenerFilterReceivesFirstPacketWithCmsg) {
  useAccessLog(fmt::format("%FILTER_STATE({}:PLAIN)%", TestFirstPacketReceivedFilterState::key()));
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    envoy::config::core::v3::SocketCmsgHeaders* cmsg =
        listener->mutable_udp_listener_config()->mutable_quic_options()->add_save_cmsg_config();
    cmsg->mutable_level()->set_value(GetParam() == Network::Address::IpVersion::v4 ? 0 : 41);
    cmsg->mutable_type()->set_value(GetParam() == Network::Address::IpVersion::v4 ? 1 : 50);
    cmsg->set_expected_size(128);
    auto* listener_filter = listener->add_listener_filters();
    listener_filter->set_name("envoy.filters.quic_listener.test");
    auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
    configuration.set_added_value("foo");
    configuration.set_allow_server_migration(false);
    configuration.set_allow_client_migration(false);
    listener_filter->mutable_typed_config()->PackFrom(configuration);
  });
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  codec_client_->close();

  std::string log = waitForAccessLog(access_log_name_, 0);
  // Log format defined in TestFirstPacketReceivedFilterState::serializeAsString.
  std::vector<std::string> metrics = absl::StrSplit(log, ',');
  ASSERT_EQ(metrics.size(), 3);
  // onFirstPacketReceived was called only once.
  EXPECT_EQ(std::stoi(metrics.at(0)), 1);
  // first packet has length greater than zero.
  EXPECT_GT(std::stoi(metrics.at(1)), 0);
  // first packet has packet headers length greater than zero.
  EXPECT_GT(std::stoi(metrics.at(2)), 0);
}

} // namespace Quic
} // namespace Envoy
