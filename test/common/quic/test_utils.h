#pragma once

#include "envoy/common/optref.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/quic/envoy_quic_client_connection.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"
#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/first_flight.h"
#include "quiche/quic/test_tools/qpack/qpack_test_utils.h"
#include "quiche/quic/test_tools/quic_config_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

class MockEnvoyQuicServerConnection : public EnvoyQuicServerConnection {
public:
  MockEnvoyQuicServerConnection(quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter& writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Network::Socket& listen_socket,
                                quic::ConnectionIdGeneratorInterface& generator)
      : MockEnvoyQuicServerConnection(
            helper, alarm_factory, writer,
            quic::QuicSocketAddress(quic::QuicIpAddress::Any4(), 12345),
            quic::QuicSocketAddress(quic::QuicIpAddress::Loopback4(), 12345), supported_versions,
            listen_socket, generator) {}

  MockEnvoyQuicServerConnection(
      quic::QuicConnectionHelperInterface& helper, quic::QuicAlarmFactory& alarm_factory,
      quic::QuicPacketWriter& writer, quic::QuicSocketAddress self_address,
      quic::QuicSocketAddress peer_address, const quic::ParsedQuicVersionVector& supported_versions,
      Network::Socket& listen_socket, quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicServerConnection(
            quic::test::TestConnectionId(), self_address, peer_address, helper, alarm_factory,
            &writer, /*owns_writer=*/false, supported_versions,
            createServerConnectionSocket(listen_socket.ioHandle(), self_address, peer_address,
                                         "example.com", "h3-29"),
            generator, nullptr) {}

  Network::Connection::ConnectionStats& connectionStats() const {
    return QuicNetworkConnection::connectionStats();
  }

  MOCK_METHOD(void, SendConnectionClosePacket,
              (quic::QuicErrorCode, quic::QuicIetfTransportErrorCodes, const std::string&));
  MOCK_METHOD(bool, SendControlFrame, (const quic::QuicFrame& frame));
  MOCK_METHOD(quic::MessageStatus, SendMessage,
              (quic::QuicMessageId, absl::Span<quiche::QuicheMemSlice>, bool));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));
};

class MockEnvoyQuicClientConnection : public EnvoyQuicClientConnection {
public:
  MockEnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter* writer, bool owns_writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Event::Dispatcher& dispatcher,
                                Network::ConnectionSocketPtr&& connection_socket,
                                quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, writer, owns_writer,
                                  supported_versions, dispatcher, std::move(connection_socket),
                                  generator, /*prefer_gro=*/true) {}

  MOCK_METHOD(quic::MessageStatus, SendMessage,
              (quic::QuicMessageId, absl::Span<quiche::QuicheMemSlice>, bool));
};

class TestQuicCryptoStream : public quic::test::MockQuicCryptoStream {
public:
  explicit TestQuicCryptoStream(quic::QuicSession* session)
      : quic::test::MockQuicCryptoStream(session) {}

  bool encryption_established() const override { return true; }
};

class MockEnvoyQuicSession : public quic::QuicSpdySession, public QuicFilterManagerConnectionImpl {
public:
  MockEnvoyQuicSession(const quic::QuicConfig& config,
                       const quic::ParsedQuicVersionVector& supported_versions,
                       EnvoyQuicServerConnection* connection, Event::Dispatcher& dispatcher,
                       uint32_t send_buffer_limit, QuicStatNames& quic_stat_names,
                       Stats::Scope& scope)
      : quic::QuicSpdySession(connection, /*visitor=*/nullptr, config, supported_versions),
        QuicFilterManagerConnectionImpl(
            *connection, connection->connection_id(), dispatcher, send_buffer_limit, {nullptr},
            std::make_unique<StreamInfo::StreamInfoImpl>(
                dispatcher.timeSource(),
                connection->connectionSocket()->connectionInfoProviderSharedPtr(),
                StreamInfo::FilterState::LifeSpan::Connection),
            quic_stat_names, scope),
        crypto_stream_(std::make_unique<TestQuicCryptoStream>(this)) {}

  void Initialize() override {
    quic::QuicSpdySession::Initialize();
    initialized_ = true;
  }

  // From QuicSession.
  MOCK_METHOD(quic::QuicSpdyStream*, CreateIncomingStream, (quic::QuicStreamId id));
  MOCK_METHOD(quic::QuicSpdyStream*, CreateIncomingStream, (quic::PendingStream * pending));
  MOCK_METHOD(quic::QuicSpdyStream*, CreateOutgoingBidirectionalStream, ());
  MOCK_METHOD(quic::QuicSpdyStream*, CreateOutgoingUnidirectionalStream, ());
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (quic::QuicStreamId id));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, ());
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, ());
  MOCK_METHOD(quic::QuicConsumedData, WritevData,
              (quic::QuicStreamId id, size_t write_length, quic::QuicStreamOffset offset,
               quic::StreamSendingState state, quic::TransmissionType type,
               quic::EncryptionLevel level));
  MOCK_METHOD(bool, ShouldYield, (quic::QuicStreamId id));
  MOCK_METHOD(void, MaybeSendRstStreamFrame,
              (quic::QuicStreamId id, quic::QuicResetStreamError error,
               quic::QuicStreamOffset bytes_written));
  MOCK_METHOD(void, MaybeSendStopSendingFrame,
              (quic::QuicStreamId id, quic::QuicResetStreamError error));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));

  absl::string_view requestedServerName() const override {
    return {GetCryptoStream()->crypto_negotiated_params().sni};
  }

  quic::QuicCryptoStream* GetMutableCryptoStream() override { return crypto_stream_.get(); }

  const quic::QuicCryptoStream* GetCryptoStream() const override { return crypto_stream_.get(); }

  using quic::QuicSpdySession::ActivateStream;

protected:
  quic::HttpDatagramSupport LocalHttpDatagramSupport() override {
    return quic::HttpDatagramSupport::kRfc;
  }
  bool hasDataToWrite() override { return HasDataToWrite(); }
  const quic::QuicConnection* quicConnection() const override {
    return initialized_ ? connection() : nullptr;
  }
  quic::QuicConnection* quicConnection() override { return initialized_ ? connection() : nullptr; }

private:
  std::unique_ptr<quic::QuicCryptoStream> crypto_stream_;
};

class TestQuicCryptoClientStream : public quic::QuicCryptoClientStream {
public:
  TestQuicCryptoClientStream(const quic::QuicServerId& server_id, quic::QuicSession* session,
                             std::unique_ptr<quic::ProofVerifyContext> verify_context,
                             quic::QuicCryptoClientConfig* crypto_config,
                             ProofHandler* proof_handler, bool has_application_state)
      : quic::QuicCryptoClientStream(server_id, session, std::move(verify_context), crypto_config,
                                     proof_handler, has_application_state) {}

  bool encryption_established() const override { return true; }
  quic::HandshakeState GetHandshakeState() const override { return quic::HANDSHAKE_CONFIRMED; }
};

class TestQuicCryptoClientStreamFactory : public EnvoyQuicCryptoClientStreamFactoryInterface {
public:
  std::unique_ptr<quic::QuicCryptoClientStreamBase>
  createEnvoyQuicCryptoClientStream(const quic::QuicServerId& server_id, quic::QuicSession* session,
                                    std::unique_ptr<quic::ProofVerifyContext> verify_context,
                                    quic::QuicCryptoClientConfig* crypto_config,
                                    quic::QuicCryptoClientStream::ProofHandler* proof_handler,
                                    bool has_application_state) override {
    last_verify_context_ = *verify_context;
    return std::make_unique<TestQuicCryptoClientStream>(server_id, session,
                                                        std::move(verify_context), crypto_config,
                                                        proof_handler, has_application_state);
  }

  OptRef<quic::ProofVerifyContext> lastVerifyContext() const { return last_verify_context_; }

private:
  OptRef<quic::ProofVerifyContext> last_verify_context_;
};

class IsolatedStoreProvider {
protected:
  Stats::IsolatedStoreImpl stats_store_;
};

class MockEnvoyQuicClientSession : public IsolatedStoreProvider, public EnvoyQuicClientSession {
public:
  MockEnvoyQuicClientSession(const quic::QuicConfig& config,
                             const quic::ParsedQuicVersionVector& supported_versions,
                             std::unique_ptr<EnvoyQuicClientConnection> connection,
                             Event::Dispatcher& dispatcher, uint32_t send_buffer_limit,
                             EnvoyQuicCryptoClientStreamFactoryInterface& crypto_stream_factory)
      : EnvoyQuicClientSession(config, supported_versions, std::move(connection),
                               quic::QuicServerId("example.com", 443),
                               std::make_shared<quic::QuicCryptoClientConfig>(
                                   quic::test::crypto_test_utils::ProofVerifierForTesting()),
                               dispatcher, send_buffer_limit, crypto_stream_factory,
                               quic_stat_names_, {}, *stats_store_.rootScope(), nullptr, {}) {}

  void Initialize() override {
    EnvoyQuicClientSession::Initialize();
    initialized_ = true;
  }

  // From QuicSession.
  MOCK_METHOD(quic::QuicSpdyClientStream*, CreateIncomingStream, (quic::QuicStreamId id));
  MOCK_METHOD(quic::QuicSpdyClientStream*, CreateIncomingStream, (quic::PendingStream * pending));
  MOCK_METHOD(quic::QuicSpdyClientStream*, CreateOutgoingBidirectionalStream, ());
  MOCK_METHOD(quic::QuicSpdyClientStream*, CreateOutgoingUnidirectionalStream, ());
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (quic::QuicStreamId id));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, ());
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, ());
  MOCK_METHOD(quic::QuicConsumedData, WritevData,
              (quic::QuicStreamId id, size_t write_length, quic::QuicStreamOffset offset,
               quic::StreamSendingState state, quic::TransmissionType type,
               quic::EncryptionLevel level));
  MOCK_METHOD(bool, ShouldYield, (quic::QuicStreamId id));
  MOCK_METHOD(void, dumpState, (std::ostream&, int), (const));

  absl::string_view requestedServerName() const override {
    return {GetCryptoStream()->crypto_negotiated_params().sni};
  }

  using quic::QuicSession::closed_streams;
  using quic::QuicSpdySession::ActivateStream;

protected:
  quic::HttpDatagramSupport LocalHttpDatagramSupport() override {
    return quic::HttpDatagramSupport::kRfc;
  }
  bool hasDataToWrite() override { return HasDataToWrite(); }
  const quic::QuicConnection* quicConnection() const override {
    return initialized_ ? connection() : nullptr;
  }
  quic::QuicConnection* quicConnection() override { return initialized_ ? connection() : nullptr; }

  QuicStatNames quic_stat_names_{stats_store_.symbolTable()};
};

Buffer::OwnedImpl generateChloPacketToSend(quic::ParsedQuicVersion quic_version,
                                           quic::QuicConfig& quic_config,
                                           quic::QuicConnectionId connection_id) {
  std::unique_ptr<quic::QuicReceivedPacket> packet =
      std::move(quic::test::GetFirstFlightOfPackets(quic_version, quic_config, connection_id)[0]);
  return {packet->data(), packet->length()};
}

void setQuicConfigWithDefaultValues(quic::QuicConfig* config) {
  quic::test::QuicConfigPeer::SetReceivedMaxBidirectionalStreams(
      config, quic::kDefaultMaxStreamsPerConnection);
  quic::test::QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(
      config, quic::kDefaultMaxStreamsPerConnection);
  quic::test::QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
      config, quic::kMinimumFlowControlSendWindow);
  quic::test::QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
      config, quic::kMinimumFlowControlSendWindow);
  quic::test::QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
      config, quic::kMinimumFlowControlSendWindow);
  quic::test::QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
      config, quic::kMinimumFlowControlSendWindow);
}

std::string spdyHeaderToHttp3StreamPayload(const quiche::HttpHeaderBlock& header) {
  quic::test::NoopQpackStreamSenderDelegate encoder_stream_sender_delegate;
  quic::NoopDecoderStreamErrorDelegate decoder_stream_error_delegate;
  auto qpack_encoder = std::make_unique<quic::QpackEncoder>(&decoder_stream_error_delegate,
                                                            quic::HuffmanEncoding::kEnabled,
                                                            quic::CookieCrumbling::kEnabled);
  qpack_encoder->set_qpack_stream_sender_delegate(&encoder_stream_sender_delegate);
  // QpackEncoder does not use the dynamic table by default,
  // therefore the value of |stream_id| does not matter.
  std::string payload = qpack_encoder->EncodeHeaderList(/* stream_id = */ 0, header, nullptr);
  std::string headers_frame_header =
      quic::HttpEncoder::SerializeHeadersFrameHeader(payload.length());
  return absl::StrCat(headers_frame_header, payload);
}

std::string bodyToHttp3StreamPayload(const std::string& body) {
  quiche::SimpleBufferAllocator allocator;
  quiche::QuicheBuffer header =
      quic::HttpEncoder::SerializeDataFrameHeader(body.length(), &allocator);
  return absl::StrCat(header.AsStringView(), body);
}

// A test suite with variation of ip version and a knob to turn on/off IETF QUIC implementation.
class QuicMultiVersionTest : public testing::TestWithParam<
                                 std::pair<Network::Address::IpVersion, quic::ParsedQuicVersion>> {
};

std::vector<std::pair<Network::Address::IpVersion, quic::ParsedQuicVersion>> generateTestParam() {
  std::vector<std::pair<Network::Address::IpVersion, quic::ParsedQuicVersion>> param;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (const auto& quic_version : quic::CurrentSupportedHttp3Versions()) {
      param.emplace_back(ip_version, quic_version);
    }
  }
  return param;
}

std::string testParamsToString(
    const ::testing::TestParamInfo<std::pair<Network::Address::IpVersion, quic::ParsedQuicVersion>>&
        params) {
  return absl::StrCat(TestUtility::ipVersionToString(params.param.first),
                      quic::QuicVersionToString(params.param.second.transport_version));
}

class MockProofVerifyContext : public EnvoyQuicProofVerifyContext {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (const));
  MOCK_METHOD(bool, isServer, (), (const));
  MOCK_METHOD(const Network::TransportSocketOptionsConstSharedPtr&, transportSocketOptions, (),
              (const));
  MOCK_METHOD(Extensions::TransportSockets::Tls::CertValidator::ExtraValidationContext,
              extraValidationContext, (), (const));
};

class MockQuicConnectionDebugVisitor : public quic::QuicConnectionDebugVisitor {
public:
  MockQuicConnectionDebugVisitor(quic::QuicSession* session,
                                 const StreamInfo::StreamInfo& stream_info)
      : session_(session), stream_info_(stream_info) {}

  MOCK_METHOD(void, OnConnectionClosed,
              (const quic::QuicConnectionCloseFrame&, quic::ConnectionCloseSource), ());
  MOCK_METHOD(void, OnConnectionCloseFrame, (const quic::QuicConnectionCloseFrame&), ());

  quic::QuicSession* session_;
  const StreamInfo::StreamInfo& stream_info_;
};

class TestEnvoyQuicConnectionDebugVisitorFactory
    : public EnvoyQuicConnectionDebugVisitorFactoryInterface {
public:
  std::string name() const override { return "envoy.quic.connection_debug_visitor.mock"; }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<::test::common::config::DummyConfig>();
  }
  std::unique_ptr<quic::QuicConnectionDebugVisitor>
  createQuicConnectionDebugVisitor(quic::QuicSession* session,
                                   const StreamInfo::StreamInfo& stream_info) override {
    auto debug_visitor = std::make_unique<MockQuicConnectionDebugVisitor>(session, stream_info);
    mock_debug_visitor_ = debug_visitor.get();
    return debug_visitor;
  }

  Envoy::ProcessContextOptRef processContext() const { return context_; }

  MockQuicConnectionDebugVisitor* mock_debug_visitor_;
};

DECLARE_FACTORY(TestEnvoyQuicConnectionDebugVisitorFactory);

REGISTER_FACTORY(TestEnvoyQuicConnectionDebugVisitorFactory,
                 Envoy::Quic::EnvoyQuicConnectionDebugVisitorFactoryInterface);

class TestNetworkObserverRegistry : public Quic::EnvoyQuicNetworkObserverRegistry {
public:
  void onNetworkChanged() {
    std::list<Quic::QuicNetworkConnectivityObserver*> existing_observers;
    for (Quic::QuicNetworkConnectivityObserver* observer : registeredQuicObservers()) {
      existing_observers.push_back(observer);
    }
    for (auto* observer : existing_observers) {
      observer->onNetworkChanged();
    }
  }
  using Quic::EnvoyQuicNetworkObserverRegistry::registeredQuicObservers;
};

} // namespace Quic
} // namespace Envoy
