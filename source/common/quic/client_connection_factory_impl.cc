#include "source/common/quic/client_connection_factory_impl.h"

#include "source/common/quic/quic_transport_socket_factory.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace Envoy {
namespace Quic {

// A wrapper class for quic::SessionCache.
class QuicSessionCacheDelegate : public quic::SessionCache {
public:
  explicit QuicSessionCacheDelegate(quic::SessionCache& cache) : cache_(cache) {}

  void Insert(const quic::QuicServerId& server_id, bssl::UniquePtr<SSL_SESSION> session,
              const quic::TransportParameters& params,
              const quic::ApplicationState* application_state) override {
    cache_.Insert(server_id, std::move(session), params, application_state);
  }

  std::unique_ptr<quic::QuicResumptionState>
  Lookup(const quic::QuicServerId& server_id, quic::QuicWallTime now, const SSL_CTX* ctx) override {
    auto state = cache_.Lookup(server_id, now, ctx);
    return state;
  }

  void ClearEarlyData(const quic::QuicServerId& server_id) override {
    cache_.ClearEarlyData(server_id);
  }

  void OnNewTokenReceived(const quic::QuicServerId& server_id, absl::string_view token) override {
    cache_.OnNewTokenReceived(server_id, token);
  }

  void RemoveExpiredEntries(quic::QuicWallTime now) override { cache_.RemoveExpiredEntries(now); }

  void Clear() override { cache_.Clear(); }

private:
  quic::SessionCache& cache_;
};

PersistentQuicInfoImpl::PersistentQuicInfoImpl(Event::Dispatcher& dispatcher, uint32_t buffer_limit)
    : conn_helper_(dispatcher), alarm_factory_(dispatcher, *conn_helper_.GetClock()),
      buffer_limit_(buffer_limit) {
  quiche::FlagRegistry::getInstance();
}

std::unique_ptr<quic::SessionCache> PersistentQuicInfoImpl::getQuicSessionCacheDelegate() {
  return std::make_unique<QuicSessionCacheDelegate>(session_cache_);
}

std::unique_ptr<Network::ClientConnection>
createQuicNetworkConnection(Http::PersistentQuicInfo& info,
                            std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config,
                            const quic::QuicServerId& server_id, Event::Dispatcher& dispatcher,
                            Network::Address::InstanceConstSharedPtr server_addr,
                            Network::Address::InstanceConstSharedPtr local_addr,
                            QuicStatNames& quic_stat_names, Stats::Scope& scope) {
  ASSERT(GetQuicReloadableFlag(quic_single_ack_in_packet2));
  ASSERT(crypto_config != nullptr);
  PersistentQuicInfoImpl* info_impl = reinterpret_cast<PersistentQuicInfoImpl*>(&info);
  quic::ParsedQuicVersionVector quic_versions = quic::CurrentSupportedHttp3Versions();
  ASSERT(!quic_versions.empty());
  auto connection = std::make_unique<EnvoyQuicClientConnection>(
      quic::QuicUtils::CreateRandomConnectionId(), server_addr, info_impl->conn_helper_,
      info_impl->alarm_factory_, quic_versions, local_addr, dispatcher, nullptr);

  // QUICHE client session always use the 1st version to start handshake.
  auto ret = std::make_unique<EnvoyQuicClientSession>(
      info_impl->quic_config_, quic_versions, std::move(connection), server_id,
      std::move(crypto_config), &info_impl->push_promise_index_, dispatcher,
      info_impl->buffer_limit_, info_impl->crypto_stream_factory_, quic_stat_names, scope);
  return ret;
}

} // namespace Quic
} // namespace Envoy
