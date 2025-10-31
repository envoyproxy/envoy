#pragma once

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
#include "quiche/quic/core/http/quic_connection_migration_manager.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
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
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter* writer, bool owns_writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Event::Dispatcher& dispatcher,
                                Network::ConnectionSocketPtr&& connection_socket,
                                quic::ConnectionIdGeneratorInterface& generator,
                                bool validation_failure_on_path_response)
      : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, writer, owns_writer,
                                  supported_versions, dispatcher, std::move(connection_socket),
                                  generator),
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
                     Buffer::OwnedImpl saved_cmsg) override {
    last_local_address_ = local_address;
    last_peer_address_ = peer_address;
    EnvoyQuicClientConnection::processPacket(local_address, peer_address, std::move(buffer),
                                             receive_time, tos, std::move(saved_cmsg));
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

  void configureEarlyData(bool enabled, ConfigHelper* config_helper = nullptr) {
    if (config_helper == nullptr) {
      config_helper = &config_helper_;
    }
    config_helper->addConfigModifier([enabled](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ts = bootstrap.mutable_static_resources()
                     ->mutable_listeners(0)
                     ->mutable_filter_chains(0)
                     ->mutable_transport_socket();

      auto quic_transport_socket_config = MessageUtil::anyConvert<
          envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport>(
          *ts->mutable_typed_config());
      quic_transport_socket_config.mutable_enable_early_data()->set_value(enabled);
      ts->mutable_typed_config()->PackFrom(quic_transport_socket_config);
    });
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
    auto& persistent_info =
        static_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
    QuicClientPacketWriterFactory::CreationResult creation_result =
        persistent_info.writer_factory_->createSocketAndQuicPacketWriter(
            server_addr_, quic::kInvalidNetworkHandle, local_addr, options);
    quic::QuicForceBlockablePacketWriter* wrapper = nullptr;
    if (quiche_handles_migration_) {
      wrapper = new quic::QuicForceBlockablePacketWriter();
      // Owns the inner writer.
      wrapper->set_writer(creation_result.writer_.release());
    }
    auto connection = std::make_unique<TestEnvoyQuicClientConnection>(
        getNextConnectionId(), conn_helper_, alarm_factory_,
        (quiche_handles_migration_
             ? wrapper
             : static_cast<quic::QuicPacketWriter*>(creation_result.writer_.release())),
        /*owns_writer=*/true, quic::ParsedQuicVersionVector{supported_versions_[0]}, *dispatcher_,
        std::move(creation_result.socket_), connection_id_generator_,
        validation_failure_on_path_response_);
    EnvoyQuicClientConnection::EnvoyQuicMigrationHelper* migration_helper = nullptr;
    if (quiche_handles_migration_) {
      migration_helper =
          &connection->getOrCreateMigrationHelper(*persistent_info.writer_factory_, {});
    } else {
      connection->setWriterFactory(*persistent_info.writer_factory_);
    }
    quic_connection_ = connection.get();
    ASSERT(quic_connection_persistent_info_ != nullptr);
    OptRef<Http::HttpServerPropertiesCache> cache;
    auto session = std::make_unique<EnvoyQuicClientSession>(
        persistent_info.quic_config_, supported_versions_, std::move(connection), wrapper,
        migration_helper, migration_config_,
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
      bool wait_for_1rtt_key, absl::optional<bool> disable_qpack = absl::nullopt) {
    std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
    cluster->max_response_headers_count_ = 200;
    if (http2_options.has_value()) {
      cluster->http3_options_ = ConfigHelper::http2ToHttp3ProtocolOptions(
          http2_options.value(), quic::kStreamReceiveWindowLimit);
    }
    *cluster->http3_options_.mutable_quic_protocol_options() = client_quic_options_;
    if (disable_qpack.has_value()) {
      cluster->http3_options_.set_disable_qpack(*disable_qpack);
    }
    Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
        cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)))};
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
    setListenersBoundTimeout(TestUtility::DefaultTimeout * 15);
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
    ON_CALL(context.server_context_, sslContextManager())
        .WillByDefault(testing::ReturnRef(context_manager_));
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
    constexpr auto timeout_first = std::chrono::seconds(15 * TIMEOUT_FACTOR);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10 * TIMEOUT_FACTOR);
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
  quic::QuicConnectionMigrationConfig migration_config_{quicConnectionMigrationDisableAllConfig()};
  bool quiche_handles_migration_{false};
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

  void testMultipleQuicConnections(
      std::function<quic::QuicConnectionId(size_t index)> gen_connection_id = [](size_t index) {
        // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection
        // id
        // in the packet header. And currently all QUIC versions support >= 8 bytes connection id.
        // So create connections with the first 4 bytes of connection id different from each other
        // so they should be evenly distributed.
        return quic::test::TestConnectionId(index << 32);
      }) {
    // Enabling SO_REUSEPORT with 8 workers. Unfortunately this setting makes the test rarely flaky
    // if it is configured to run with --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);
    initialize();
    std::vector<std::string> addresses({"address1", "address2"});
    registerTestServerPorts(addresses);
    std::vector<IntegrationCodecClientPtr> codec_clients1;
    std::vector<IntegrationCodecClientPtr> codec_clients2;
    for (size_t i = 1; i <= concurrency_; ++i) {
      designated_connection_ids_.push_back(gen_connection_id(i));
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
      designated_connection_ids_.push_back(gen_connection_id(i));
      codec_clients2.push_back(makeHttpConnection(lookupPort("address2")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15 * TIMEOUT_FACTOR);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10 * TIMEOUT_FACTOR);
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

} // namespace Quic
} // namespace Envoy
