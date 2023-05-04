#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_session.h"

#include "test/common/quic/envoy_quic_h3_fuzz.pb.h"
#include "test/common/quic/envoy_quic_h3_fuzz_helper.h"
#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_utils.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

using namespace test::common::quic;

// The following classes essentially mock the `QUIC` handshake
class ProofSourceDetailsSetter {
public:
  virtual ~ProofSourceDetailsSetter() = default;
  virtual void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) = 0;
};

class TestQuicCryptoServerStream : public quic::QuicCryptoServerStream,
                                   public ProofSourceDetailsSetter {
public:
  ~TestQuicCryptoServerStream() override = default;
  explicit TestQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                      quic::QuicCompressedCertsCache* compressed_certs_cache,
                                      quic::QuicSession* session,
                                      quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}
  bool encryption_established() const override { return true; }
  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }
  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
};

class TestEnvoyQuicTlsServerHandshaker : public quic::TlsServerHandshaker,
                                         public ProofSourceDetailsSetter {
public:
  ~TestEnvoyQuicTlsServerHandshaker() override = default;
  TestEnvoyQuicTlsServerHandshaker(quic::QuicSession* session,
                                   const quic::QuicCryptoServerConfig& crypto_config)
      : quic::TlsServerHandshaker(session, &crypto_config),
        params_(new quic::QuicCryptoNegotiatedParameters) {
    params_->cipher_suite = 1;
  }
  bool encryption_established() const override { return true; }
  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }
  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }
  const quic::QuicCryptoNegotiatedParameters& crypto_negotiated_params() const override {
    return *params_;
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
  quiche::QuicheReferenceCountedPointer<quic::QuicCryptoNegotiatedParameters> params_;
};

class EnvoyQuicTestCryptoServerStreamFactory : public EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  std::string name() const override { return "quic.test_crypto_server_stream"; }

  std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      OptRef<const Network::DownstreamTransportSocketFactory> /*transport_socket_factory*/,
      Event::Dispatcher& /*dispatcher*/) override {
    switch (session->connection()->version().handshake_protocol) {
    case quic::PROTOCOL_QUIC_CRYPTO:
      return std::make_unique<TestQuicCryptoServerStream>(crypto_config, compressed_certs_cache,
                                                          session, helper);
    case quic::PROTOCOL_TLS1_3:
      return std::make_unique<TestEnvoyQuicTlsServerHandshaker>(session, *crypto_config);
    case quic::PROTOCOL_UNSUPPORTED:
      ASSERT(false, "Unknown handshake protocol");
    }
    return nullptr;
  }
};

envoy::config::core::v3::Http3ProtocolOptions http3Settings() { return {}; }

QuicDispatcherStats generateStats(Stats::Scope& store) {
  return {QUIC_DISPATCHER_STATS(POOL_COUNTER_PREFIX(store, "quic.dispatcher"))};
}

// `FuzzDispatcher` implements a `QuicDispatcher`, creating a session when a
// `QUIC` packet is received. It creates an `EnvoyQuicServerConnection`, where
// a no op `encrypter` and a no op `decrypter` are installed. The session is
// created and subsequently fuzzed by submitting a collection of `QUIC` and
// `HTTP/3` packets serialized to wire format to the `ProcessPacket` method.
// The fuzzer should be able to cover all code below the
// `EnvoyQuicServerSession` class, including the `QUIC` and `HTTP/3` codecs.

class FuzzQuicDispatcher : public quic::QuicDispatcher {
public:
  FuzzQuicDispatcher(const quic::ParsedQuicVersion quic_version,
                     quic::QuicVersionManager* version_manager,
                     std::unique_ptr<quic::QuicConnectionHelperInterface> connection_helper,
                     std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
                     Event::Dispatcher& dispatcher)
      : quic::QuicDispatcher(
            &quic_config_, &crypto_config_, version_manager, std::move(connection_helper),
            std::make_unique<EnvoyQuicCryptoServerStreamHelper>(), std::move(alarm_factory),
            quic::kQuicDefaultConnectionIdLength, generator_),
        listener_config_(&mock_listener_config_),
        quic_stats_(generateStats(listener_config_->listenerScope())), dispatcher_(dispatcher),
        http3_options_(http3Settings()), quic_version_(quic_version),
        packetizer_(quic_version_, helper()),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::make_unique<TestProofSource>(), quic::KeyExchangeSource::Default()),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        cli_addr_(peer_addr_->sockAddr(), peer_addr_->sockAddrLen()),
        srv_addr_(self_addr_->sockAddr(), self_addr_->sockAddrLen()),
        quic_stat_names_(listener_config_->listenerScope().symbolTable()),
        http3_stats_({ALL_HTTP3_CODEC_STATS(
            POOL_COUNTER_PREFIX(listener_config_->listenerScope(), "http3."),
            POOL_GAUGE_PREFIX(listener_config_->listenerScope(), "http3."))}),
        connection_stats_({QUIC_CONNECTION_STATS(
            POOL_COUNTER_PREFIX(listener_config_->listenerScope(), "quic.connection"))}) {
    ON_CALL(http_connection_callbacks_, newStream(_, _))
        .WillByDefault(Invoke([&](Http::ResponseEncoder&, bool) -> Http::RequestDecoder& {
          return orphan_request_decoder_;
        }));
    auto writer = new testing::NiceMock<quic::test::MockPacketWriter>();
    ON_CALL(*writer, WritePacket(_, _, _, _, _))
        .WillByDefault(testing::Return(quic::WriteResult(quic::WRITE_STATUS_OK, 0)));
    InitializeWithWriter(writer);
  }

  void fuzzQuic(const QuicH3FuzzCase& input) {
    quic::QuicReceivedPacket first_packet(nullptr, 0, helper()->GetClock()->Now());
    quic::ParsedClientHello chlo;
    quic::ReceivedPacketInfo chlo_packet_info(srv_addr_, cli_addr_, first_packet);
    chlo_packet_info.destination_connection_id = quic::test::TestConnectionId();
    chlo_packet_info.version = quic_version_;
    SetQuicFlag(quic_allow_chlo_buffering, false);
    ProcessChlo(chlo, &chlo_packet_info);

    packetizer_.serializePackets(input);
    packetizer_.foreach ([this](const char* payload, size_t size) {
      auto receipt_time = helper()->GetClock()->Now();
      quic::QuicReceivedPacket p(payload, size, receipt_time, false);
      ProcessPacket(srv_addr_, cli_addr_, p);
    });
  }

  void reset() {
    packetizer_.reset();
    Shutdown();
  }

  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override {
    quic::QuicDispatcher::OnConnectionClosed(connection_id, error, error_details, source);
  }
  quic::QuicTimeWaitListManager* CreateQuicTimeWaitListManager() override {
    return new EnvoyQuicTimeWaitListManager(writer(), this, helper()->GetClock(), alarm_factory(),
                                            quic_stats_);
  }

  void closeConnectionsWithFilterChain(const Network::FilterChain*) {}

  void updateListenerConfig(Network::ListenerConfig& new_listener_config) {
    listener_config_ = &new_listener_config;
  }

protected:
  std::unique_ptr<quic::QuicSession>
  CreateQuicSession(quic::QuicConnectionId server_connection_id,
                    const quic::QuicSocketAddress& self_address,
                    const quic::QuicSocketAddress& peer_address, absl::string_view /*alpn*/,
                    const quic::ParsedQuicVersion& version,
                    const quic::ParsedClientHello& /*parsed_chlo*/) override {

    auto connection_socket = Quic::createConnectionSocket(peer_addr_, self_addr_, nullptr);
    auto connection = std::make_unique<EnvoyQuicServerConnection>(
        server_connection_id, self_address, peer_address, *helper(), *alarm_factory(), writer(),
        false, quic::ParsedQuicVersionVector{quic_version_}, std::move(connection_socket),
        connection_id_generator());

    auto decrypter = std::make_unique<FuzzDecrypter>();
    auto encrypter = std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_CLIENT);
    connection->InstallDecrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
                                 std::move(decrypter));
    connection->SetEncrypter(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE,
                             std::move(encrypter));

    connection->SetDefaultEncryptionLevel(quic::EncryptionLevel::ENCRYPTION_FORWARD_SECURE);

    auto stream_info = std::make_unique<StreamInfo::StreamInfoImpl>(
        dispatcher_.timeSource(),
        connection->connectionSocket()->connectionInfoProviderSharedPtr());
    auto session = std::make_unique<EnvoyQuicServerSession>(
        quic_config_, quic::ParsedQuicVersionVector{version}, std::move(connection), this,
        &crypto_stream_helper_, &crypto_config_, &compressed_certs_cache_, dispatcher_,
        quic::kDefaultFlowControlSendWindow * 1.5, quic_stat_names_,
        listener_config_->listenerScope(), crypto_stream_factory_, std::move(stream_info),
        connection_stats_);
    session->Initialize();
    session->setHeadersWithUnderscoreAction(envoy::config::core::v3::HttpProtocolOptions::ALLOW);
    session->setHttp3Options(http3_options_);
    session->setCodecStats(http3_stats_);
    session->setHttpConnectionCallbacks(http_connection_callbacks_);
    session->setMaxIncomingHeadersCount(100);
    session->set_max_inbound_header_list_size(64 * 1024u);
    setQuicConfigWithDefaultValues(session->config());
    session->OnConfigNegotiated();
    return session;
  }

private:
  NiceMock<Network::MockListenerConfig> mock_listener_config_;
  Network::ListenerConfig* listener_config_{};
  QuicDispatcherStats quic_stats_;
  Event::Dispatcher& dispatcher_;

  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  quic::ParsedQuicVersion quic_version_;
  QuicPacketizer packetizer_;
  const quic::QuicCryptoServerConfig crypto_config_;
  NiceMock<quic::test::MockQuicCryptoServerStreamHelper> crypto_stream_helper_;
  Network::Address::InstanceConstSharedPtr peer_addr_;
  Network::Address::InstanceConstSharedPtr self_addr_;
  quic::QuicSocketAddress cli_addr_;
  quic::QuicSocketAddress srv_addr_;
  QuicStatNames quic_stat_names_;
  Http::Http3::CodecStats http3_stats_;
  QuicConnectionStats connection_stats_;

  quic::QuicConfig quic_config_;
  quic::DeterministicConnectionIdGenerator generator_{quic::kQuicDefaultConnectionIdLength};
  quic::QuicCompressedCertsCache compressed_certs_cache_{100};
  EnvoyQuicTestCryptoServerStreamFactory crypto_stream_factory_;

  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  NiceMock<Http::MockRequestDecoder> orphan_request_decoder_;
};

struct Harness {
  Harness(quic::ParsedQuicVersion quic_version)
      : quic_version_(quic_version), api_(Api::createApiForTest()),
        dispatcher_(api_->allocateDispatcher("envoy_quic_h3_fuzzer_thread")),
        version_manager_(quic::CurrentSupportedHttp3Versions()) {
    createDispatcher();
  }

  void createDispatcher() {
    auto connection_helper = std::unique_ptr<quic::QuicConnectionHelperInterface>(
        new EnvoyQuicConnectionHelper(*dispatcher_.get()));
    auto alarm_factory = std::unique_ptr<quic::QuicAlarmFactory>(
        new EnvoyQuicAlarmFactory(*dispatcher_.get(), *connection_helper->GetClock()));
    fuzz_quic_dispatcher_ = std::make_unique<FuzzQuicDispatcher>(
        quic_version_, &version_manager_, std::move(connection_helper), std::move(alarm_factory),
        *dispatcher_.get());
  }

  void fuzz(const test::common::quic::QuicH3FuzzCase& input) {
    fuzz_quic_dispatcher_->fuzzQuic(input);
    fuzz_quic_dispatcher_->reset();
  }

  const quic::ParsedQuicVersion quic_version_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<FuzzQuicDispatcher> fuzz_quic_dispatcher_;
  quic::QuicVersionManager version_manager_;
};

std::unique_ptr<Harness> harness;
static void resetHarness() { harness = nullptr; };
DEFINE_PROTO_FUZZER(const test::common::quic::QuicH3FuzzCase& input) {
  if (harness == nullptr) {
    harness = std::make_unique<Harness>(quic::CurrentSupportedHttp3Versions()[0]);
    atexit(resetHarness);
  }
  harness->fuzz(input);
}

} // namespace Quic
} // namespace Envoy
