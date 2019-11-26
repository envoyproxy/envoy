#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/core/quic_utils.h"

#pragma GCC diagnostic pop

namespace Envoy {
namespace Quic {

class MockEnvoyQuicSession : public quic::QuicSpdySession, public QuicFilterManagerConnectionImpl {
public:
  MockEnvoyQuicSession(const quic::QuicConfig& config,
                       const quic::ParsedQuicVersionVector& supported_versions,
                       EnvoyQuicConnection* connection, Event::Dispatcher& dispatcher,
                       uint32_t send_buffer_limit)
      : quic::QuicSpdySession(connection, /*visitor=*/nullptr, config, supported_versions),
        QuicFilterManagerConnectionImpl(connection, dispatcher, send_buffer_limit) {
    crypto_stream_ = std::make_unique<quic::test::MockQuicCryptoStream>(this);
  }

  // From QuicSession.
  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::QuicStreamId id));
  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::PendingStream* pending));
  MOCK_METHOD0(CreateOutgoingBidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD0(CreateOutgoingUnidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD1(ShouldCreateIncomingStream, bool(quic::QuicStreamId id));
  MOCK_METHOD0(ShouldCreateOutgoingBidirectionalStream, bool());
  MOCK_METHOD0(ShouldCreateOutgoingUnidirectionalStream, bool());
  MOCK_METHOD5(WritevData,
               quic::QuicConsumedData(quic::QuicStream* stream, quic::QuicStreamId id,
                                      size_t write_length, quic::QuicStreamOffset offset,
                                      quic::StreamSendingState state));

  absl::string_view requestedServerName() const override {
    return {GetCryptoStream()->crypto_negotiated_params().sni};
  }

  quic::QuicCryptoStream* GetMutableCryptoStream() override { return crypto_stream_.get(); }

  const quic::QuicCryptoStream* GetCryptoStream() const override { return crypto_stream_.get(); }

  using quic::QuicSpdySession::ActivateStream;

private:
  std::unique_ptr<quic::QuicCryptoStream> crypto_stream_;
};

} // namespace Quic
} // namespace Envoy
