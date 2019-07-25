#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/core/http/quic_server_session_base.h"

#pragma GCC diagnostic pop

#include <memory>

#include "common/common/logger.h"
#include "common/common/empty_string.h"
#include "common/network/filter_manager_impl.h"
#include "common/stream_info/stream_info_impl.h"
#include "envoy/network/connection.h"
#include "envoy/event/dispatcher.h"
#include "extensions/quic_listeners/quiche/envoy_quic_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

namespace Envoy {
namespace Quic {

// Act as a Network::Connection to HCM and a FilterManager to FilterFactoryCb.
class EnvoyQuicServerSession : public quic::QuicServerSessionBase,
                               public Network::FilterManagerConnection {
public:
  // Owns connection.
  EnvoyQuicServerSession(const quic::QuicConfig& config,
                         const quic::ParsedQuicVersionVector& supported_versions,
                         quic::QuicConnection* connection, quic::QuicSession::Visitor* visitor,
                         quic::QuicCryptoServerStream::Helper* helper,
                         const quic::QuicCryptoServerConfig* crypto_config,
                         quic::QuicCompressedCertsCache* compressed_certs_cache,
                         Event::Dispatcher& dispatcher);

  // Overridden to delete connection object.
  ~EnvoyQuicServerSession() override;

  // Network::FilterManager
  void addWriteFilter(Network::WriteFilterSharedPtr filter) override;
  void addFilter(Network::FilterSharedPtr filter) override;
  void addReadFilter(Network::ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb /*cb*/) override;
  void enableHalfClose(bool /*enabled*/) override {
    // Quic connection doesn't support half close.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void close(Network::ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  uint64_t id() const override {
    // QUIC connection id can be 18 types. It's easier to use hash value instead
    // of trying to map it into a 64-bit space.
    return connection_id().Hash();
  }
  std::string nextProtocol() const override { return EMPTY_STRING; }
  void noDelay(bool /*enable*/) override {
    // No-op. TCP_NODELAY doesn't apply to UDP.
  }
  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override;
  std::chrono::milliseconds delayedCloseTimeout() const override;
  void readDisable(bool /*disable*/) override {
    // Quic connection should be able to read through out its life time.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void detectEarlyCloseWhenReadDisabled(bool /*value*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool readEnabled() const override { return true; }
  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override;
  const Network::Address::InstanceConstSharedPtr& localAddress() const override;
  absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
  unixSocketPeerCredentials() const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) override {
    stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
    reinterpret_cast<EnvoyQuicConnection*>(connection())->setConnectionStats(stats);
  }
  const Ssl::ConnectionInfo* ssl() const override;
  Network::Connection::State state() const override {
    return connection()->connected() ? Network::Connection::State::Open
                                     : Network::Connection::State::Closed;
  }
  void write(Buffer::Instance& /*data*/, bool /*end_stream*/) override {
    // All writes should be handled by Quic internally.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setBufferLimits(uint32_t /*limit*/) override {
    // QUIC manages its own buffer.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  uint32_t bufferLimit() const override { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool localAddressRestored() const override {
    // SO_ORIGINAL_DST not supported by QUIC.
    return false;
  }
  bool aboveHighWatermark() const override {
    // QUIC doesn't have connection level buffer limit.
    return false;
  }
  const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
    // QUIC is not supported in upstream.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  absl::string_view requestedServerName() const override;
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override { return transport_failure_reason_; }
  bool isQuic() const override { return true; }

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  Network::StreamBuffer getReadBuffer() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  // Network::WriteBufferSource
  Network::StreamBuffer getWriteBuffer() override { NOT_REACHED_GCOVR_EXCL_LINE; }

  // Called by QuicHttpServerConnectionImpl before creating data streams.
  void setHttpConnectionCallbacks(Http::ServerConnectionCallbacks& callbacks) {
    http_connection_callbacks_ = &callbacks;
  }

  // quic::QuicSession
  void OnConnectionClosed(const quic::QuicConnectionCloseFrame& frame,
                          quic::ConnectionCloseSource source) override;
  void Initialize() override;

protected:
  // quic::QuicServerSessionBase
  quic::QuicCryptoServerStreamBase*
  CreateQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                               quic::QuicCompressedCertsCache* compressed_certs_cache) override;

  // quic::QuicSession
  // Overridden to create stream as encoder and associate it with an decoder.
  quic::QuicSpdyStream* CreateIncomingStream(quic::QuicStreamId id) override;
  quic::QuicSpdyStream* CreateIncomingStream(quic::PendingStream* pending) override;
  quic::QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  quic::QuicSpdyStream* CreateOutgoingUnidirectionalStream() override;

private:
  void setUpRequestDecoder(EnvoyQuicStream& stream);

  // Currently ConnectionManagerImpl is the one and only filter. If more network
  // filters are added, ConnectionManagerImpl should always be the last one.
  // Its onRead() is only called once to trigger ReadFilter::onNewConnection()
  // and the rest incoming data bypasses these filters.
  Network::FilterManagerImpl filter_manager_;
  Event::Dispatcher& dispatcher_;
  StreamInfo::StreamInfoImpl stream_info_;
  std::string transport_failure_reason_;
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> stats_;
  Http::ServerConnectionCallbacks* http_connection_callbacks_{nullptr};
  std::list<Network::ConnectionCallbacks*> network_connection_callbacks_;
};

} // namespace Quic
} // namespace Envoy
