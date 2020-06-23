#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/first_flight.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_config_peer.h"
#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Quic {

class MockEnvoyQuicSession : public quic::QuicSpdySession, public QuicFilterManagerConnectionImpl {
public:
  MockEnvoyQuicSession(const quic::QuicConfig& config,
                       const quic::ParsedQuicVersionVector& supported_versions,
                       EnvoyQuicConnection* connection, Event::Dispatcher& dispatcher,
                       uint32_t send_buffer_limit)
      : quic::QuicSpdySession(connection, /*visitor=*/nullptr, config, supported_versions),
        QuicFilterManagerConnectionImpl(*connection, dispatcher, send_buffer_limit) {
    crypto_stream_ = std::make_unique<quic::test::MockQuicCryptoStream>(this);
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
               quiche::QuicheOptional<quic::EncryptionLevel> level));
  MOCK_METHOD(bool, ShouldYield, (quic::QuicStreamId id));

  absl::string_view requestedServerName() const override {
    return {GetCryptoStream()->crypto_negotiated_params().sni};
  }

  quic::QuicCryptoStream* GetMutableCryptoStream() override { return crypto_stream_.get(); }

  const quic::QuicCryptoStream* GetCryptoStream() const override { return crypto_stream_.get(); }

  using quic::QuicSpdySession::ActivateStream;

protected:
  bool hasDataToWrite() override { return HasDataToWrite(); }

private:
  std::unique_ptr<quic::QuicCryptoStream> crypto_stream_;
};

class MockEnvoyQuicClientSession : public quic::QuicSpdyClientSession,
                                   public QuicFilterManagerConnectionImpl {
public:
  MockEnvoyQuicClientSession(const quic::QuicConfig& config,
                             const quic::ParsedQuicVersionVector& supported_versions,
                             EnvoyQuicConnection* connection, Event::Dispatcher& dispatcher,
                             uint32_t send_buffer_limit)
      : quic::QuicSpdyClientSession(config, supported_versions, connection,
                                    quic::QuicServerId("example.com", 443, false), &crypto_config_,
                                    nullptr),
        QuicFilterManagerConnectionImpl(*connection, dispatcher, send_buffer_limit),
        crypto_config_(quic::test::crypto_test_utils::ProofVerifierForTesting()) {}

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
               quiche::QuicheOptional<quic::EncryptionLevel> level));
  MOCK_METHOD(bool, ShouldYield, (quic::QuicStreamId id));

  absl::string_view requestedServerName() const override {
    return {GetCryptoStream()->crypto_negotiated_params().sni};
  }

  using quic::QuicSpdySession::ActivateStream;

protected:
  bool hasDataToWrite() override { return HasDataToWrite(); }

private:
  quic::QuicCryptoClientConfig crypto_config_;
};

Buffer::OwnedImpl
generateChloPacketToSend(quic::ParsedQuicVersion quic_version, quic::QuicConfig& quic_config,
                         quic::QuicCryptoServerConfig& crypto_config,
                         quic::QuicConnectionId connection_id, quic::QuicClock& clock,
                         const quic::QuicSocketAddress& server_address,
                         const quic::QuicSocketAddress& client_address, std::string sni) {
  if (quic_version.UsesTls()) {
    std::unique_ptr<quic::QuicReceivedPacket> packet =
        std::move(quic::test::GetFirstFlightOfPackets(quic_version, quic_config, connection_id)[0]);
    return Buffer::OwnedImpl(packet->data(), packet->length());
  }
  quic::CryptoHandshakeMessage chlo = quic::test::crypto_test_utils::GenerateDefaultInchoateCHLO(
      &clock, quic_version.transport_version, &crypto_config);
  chlo.SetVector(quic::kCOPT, quic::QuicTagVector{quic::kREJ});
  chlo.SetStringPiece(quic::kSNI, sni);
  quic::CryptoHandshakeMessage full_chlo;
  quic::QuicReferenceCountedPointer<quic::QuicSignedServerConfig> signed_config(
      new quic::QuicSignedServerConfig);
  quic::QuicCompressedCertsCache cache(
      quic::QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);
  quic::test::crypto_test_utils::GenerateFullCHLO(chlo, &crypto_config, server_address,
                                                  client_address, quic_version.transport_version,
                                                  &clock, signed_config, &cache, &full_chlo);
  // Overwrite version label to the version passed in.
  full_chlo.SetVersion(quic::kVER, quic_version);
  quic::QuicConfig quic_config_tmp;
  quic_config_tmp.ToHandshakeMessage(&full_chlo, quic_version.transport_version);

  std::string packet_content(full_chlo.GetSerialized().AsStringPiece());
  quic::ParsedQuicVersionVector supported_versions{quic_version};
  auto encrypted_packet =
      std::unique_ptr<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
          connection_id, quic::EmptyQuicConnectionId(),
          /*version_flag=*/true, /*reset_flag*/ false,
          /*packet_number=*/1, packet_content, quic::CONNECTION_ID_PRESENT,
          quic::CONNECTION_ID_ABSENT, quic::PACKET_4BYTE_PACKET_NUMBER, &supported_versions));

  return Buffer::OwnedImpl(encrypted_packet->data(), encrypted_packet->length());
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

enum class QuicVersionType {
  GquicQuicCrypto,
  GquicTls,
  Iquic,
};

// A test suite with variation of ip version and a knob to turn on/off IETF QUIC implementation.
class QuicMultiVersionTest
    : public testing::TestWithParam<std::pair<Network::Address::IpVersion, QuicVersionType>> {};

std::vector<std::pair<Network::Address::IpVersion, QuicVersionType>> generateTestParam() {
  std::vector<std::pair<Network::Address::IpVersion, QuicVersionType>> param;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    param.emplace_back(ip_version, QuicVersionType::GquicQuicCrypto);
    param.emplace_back(ip_version, QuicVersionType::GquicTls);
    param.emplace_back(ip_version, QuicVersionType::Iquic);
  }

  return param;
}

std::string testParamsToString(
    const ::testing::TestParamInfo<std::pair<Network::Address::IpVersion, QuicVersionType>>&
        params) {
  std::string ip_version = params.param.first == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6";
  switch (params.param.second) {
  case QuicVersionType::GquicQuicCrypto:
    return absl::StrCat(ip_version, "_UseGQuicWithQuicCrypto");
  case QuicVersionType::GquicTls:
    return absl::StrCat(ip_version, "_UseGQuicWithTLS");
  case QuicVersionType::Iquic:
    return absl::StrCat(ip_version, "_UseHttp3");
  }
}

} // namespace Quic
} // namespace Envoy
