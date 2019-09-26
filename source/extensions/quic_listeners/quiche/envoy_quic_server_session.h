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
                               public Network::FilterManagerConnection,
                               protected Logger::Loggable<Logger::Id::connection> {
public:
  EnvoyQuicServerSession(const quic::QuicConfig& config,
                         const quic::ParsedQuicVersionVector& supported_versions,
                         std::unique_ptr<EnvoyQuicConnection> connection,
                         quic::QuicSession::Visitor* visitor,
                         quic::QuicCryptoServerStream::Helper* helper,
                         const quic::QuicCryptoServerConfig* crypto_config,
                         quic::QuicCompressedCertsCache* compressed_certs_cache,
                         Event::Dispatcher& dispatcher);

  // Network::FilterManager
  // Overridden to delegate calls to filter_manager_.
  void addWriteFilter(Network::WriteFilterSharedPtr filter) override;
  void addFilter(Network::FilterSharedPtr filter) override;
  void addReadFilter(Network::ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(Network::ConnectionCallbacks& cb) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb /*cb*/) override;
  void enableHalfClose(bool enabled) override;
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
  void readDisable(bool disable) override {
    ASSERT(!disable, "Quic connection should be able to read through out its life time.");
  }
  void detectEarlyCloseWhenReadDisabled(bool /*value*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool readEnabled() const override { return true; }
  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override;
  const Network::Address::InstanceConstSharedPtr& localAddress() const override;
  absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
  unixSocketPeerCredentials() const override {
    ASSERT(false, "Unix domain socket is not supported.");
    return absl::nullopt;
  }
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) override {
    stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
    quic_connection_->setConnectionStats(stats);
  }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  Network::Connection::State state() const override {
    return connection()->connected() ? Network::Connection::State::Open
                                     : Network::Connection::State::Closed;
  }
  void write(Buffer::Instance& /*data*/, bool /*end_stream*/) override {
    // All writes should be handled by Quic internally.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override;
  bool localAddressRestored() const override {
    // SO_ORIGINAL_DST not supported by QUIC.
    return false;
  }
  bool aboveHighWatermark() const override {
    ENVOY_CONN_LOG(error, "QUIC doesn't have connection level write buffer limit.", *this);
    return false;
  }
  const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override;
  absl::string_view requestedServerName() const override;
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override { return transport_failure_reason_; }

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  Network::StreamBuffer getReadBuffer() override {
    // Network filter has to stop iteration to prevent hitting this line.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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
  void SendGoAway(quic::QuicErrorCode error_code, const std::string& reason) override;

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

  std::unique_ptr<EnvoyQuicConnection> quic_connection_;
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
  // These callbacks are owned by network filters and quic session should out live
  // them.
  Http::ServerConnectionCallbacks* http_connection_callbacks_{nullptr};
  std::list<Network::ConnectionCallbacks*> network_connection_callbacks_;
};

} // namespace Quic
} // namespace Envoy
