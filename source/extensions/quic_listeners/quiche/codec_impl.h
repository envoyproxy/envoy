#include "envoy/http/codec.h"
#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/http3/quic_codec_factory.h"
#include "common/http/http3/well_known_names.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

// QuicHttpConnectionImplBase instance is a thin QUIC codec just providing quic interface to HCM.
// Owned by HCM and created during onNewConnection() if the network connection
// is a QUIC connection.
class QuicHttpConnectionImplBase : public virtual Http::Connection,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicHttpConnectionImplBase(QuicFilterManagerConnectionImpl& quic_session)
      : quic_session_(quic_session) {}

  // Http::Connection
  Http::Status dispatch(Buffer::Instance& /*data*/) override {
    // Bypassed. QUIC connection already hands all data to streams.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  Http::Protocol protocol() override { return Http::Protocol::Http3; }
  // Returns true if the session has data to send but queued in connection or
  // stream send buffer.
  bool wantsToWrite() override;

  void runWatermarkCallbacksForEachStream(
      quic::QuicSmallMap<quic::QuicStreamId, std::unique_ptr<quic::QuicStream>, 10>& stream_map,
      bool high_watermark);

protected:
  QuicFilterManagerConnectionImpl& quic_session_;
};

class QuicHttpServerConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ServerConnection {
public:
  QuicHttpServerConnectionImpl(EnvoyQuicServerSession& quic_session,
                               Http::ServerConnectionCallbacks& callbacks);

  // Http::Connection
  void goAway() override;
  void shutdownNotice() override;
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override;
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

private:
  EnvoyQuicServerSession& quic_server_session_;
};

class QuicHttpClientConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ClientConnection {
public:
  QuicHttpClientConnectionImpl(EnvoyQuicClientSession& session,
                               Http::ConnectionCallbacks& callbacks);

  // Http::ClientConnection
  Http::RequestEncoder& newStream(Http::ResponseDecoder& response_decoder) override;

  // Http::Connection
  void goAway() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void shutdownNotice() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override;
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

private:
  EnvoyQuicClientSession& quic_client_session_;
};

// A factory to create QuicHttpClientConnection.
class QuicHttpClientConnectionFactoryImpl : public Http::QuicHttpClientConnectionFactory {
public:
  std::unique_ptr<Http::ClientConnection>
  createQuicClientConnection(Network::Connection& connection,
                             Http::ConnectionCallbacks& callbacks) override;

  std::string name() const override { return Http::QuicCodecNames::get().Quiche; }
};

// A factory to create QuicHttpServerConnection.
class QuicHttpServerConnectionFactoryImpl : public Http::QuicHttpServerConnectionFactory {
public:
  std::unique_ptr<Http::ServerConnection>
  createQuicServerConnection(Network::Connection& connection,
                             Http::ConnectionCallbacks& callbacks) override;

  std::string name() const override { return Http::QuicCodecNames::get().Quiche; }
};

// TODO(#14829) we should avoid creating this per-connection.
struct QuicUpstreamData {
  QuicUpstreamData(Event::Dispatcher& dispatcher, Envoy::Ssl::ClientContextConfig& config,
                   Network::Address::InstanceConstSharedPtr server_addr)
      : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
        config_(config), server_id_{config_.serverNameIndication(),
                                    static_cast<uint16_t>(server_addr->ip()->port()), false} {}

  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  Envoy::Ssl::ClientContextConfig& config_;
  quic::QuicServerId server_id_;
  std::unique_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  quic::ParsedQuicVersionVector supported_versions_{quic::CurrentSupportedVersions()};
};

class EnvoyQuicClientSessionWithExtras : public EnvoyQuicClientSession {
public:
  using EnvoyQuicClientSession::EnvoyQuicClientSession;

  std::unique_ptr<QuicUpstreamData> quic_upstream_data_;
};

// A factory to create EnvoyQuicClientConnection instance for QUIC
class QuicClientConnectionFactoryImpl : public Http::QuicClientConnectionFactory {
public:
  std::unique_ptr<Network::ClientConnection>
  createQuicNetworkConnection(Network::Address::InstanceConstSharedPtr server_addr,
                              Network::Address::InstanceConstSharedPtr local_addr,
                              Network::TransportSocketFactory& transport_socket_factory,
                              Stats::Scope& stats_scope, Event::Dispatcher& dispatcher,
                              TimeSource& time_source) override {
    // TODO(#14829): reject the config if a raw buffer socket is configured.
    auto* ssl_socket_factory =
        dynamic_cast<Extensions::TransportSockets::Tls::ClientSslSocketFactory*>(
            &transport_socket_factory);
    ASSERT(ssl_socket_factory != nullptr);

    std::unique_ptr<QuicUpstreamData> upstream_data =
        std::make_unique<QuicUpstreamData>(dispatcher, *ssl_socket_factory->config(), server_addr);
    upstream_data->crypto_config_ = std::make_unique<quic::QuicCryptoClientConfig>(
        std::make_unique<EnvoyQuicProofVerifier>(stats_scope, upstream_data->config_, time_source));

    auto connection = std::make_unique<EnvoyQuicClientConnection>(
        quic::QuicUtils::CreateRandomConnectionId(), server_addr, upstream_data->conn_helper_,
        upstream_data->alarm_factory_,
        quic::ParsedQuicVersionVector{upstream_data->supported_versions_[0]}, local_addr,
        dispatcher, nullptr);
    auto ret = std::make_unique<EnvoyQuicClientSessionWithExtras>(
        quic_config_, upstream_data->supported_versions_, std::move(connection),
        upstream_data->server_id_, upstream_data->crypto_config_.get(), &push_promise_index_,
        dispatcher, 0);
    ret->Initialize();
    ret->quic_upstream_data_ = std::move(upstream_data);
    return ret;
  }

  quic::QuicConfig quic_config_;
  std::string name() const override { return Http::QuicCodecNames::get().Quiche; }

  quic::QuicClientPushPromiseIndex push_promise_index_;
};

DECLARE_FACTORY(QuicHttpClientConnectionFactoryImpl);
DECLARE_FACTORY(QuicHttpServerConnectionFactoryImpl);
DECLARE_FACTORY(QuicClientConnectionFactoryImpl);

} // namespace Quic
} // namespace Envoy
