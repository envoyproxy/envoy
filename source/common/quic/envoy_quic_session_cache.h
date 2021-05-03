#pragma once

#include "envoy/common/time.h"

#include "quiche/quic/core/crypto/quic_crypto_client_config.h"

namespace Envoy {
namespace Quic {

// Implementation of quic::SessionCache using an Envoy time source.
class EnvoyQuicSessionCache : public quic::SessionCache {
public:
  explicit EnvoyQuicSessionCache(TimeSource& time_source);
  ~EnvoyQuicSessionCache() override;

  // From quic::SessionCache.
  void Insert(const quic::QuicServerId& server_id, bssl::UniquePtr<SSL_SESSION> session,
              const quic::TransportParameters& params,
              const quic::ApplicationState* application_state) override;
  std::unique_ptr<quic::QuicResumptionState> Lookup(const quic::QuicServerId& server_id,
                                                    const SSL_CTX* ctx) override;
  void ClearEarlyData(const quic::QuicServerId& server_id) override;

  // Returns number of entries in the cache.
  size_t size() const;

private:
  struct Entry {
    Entry();
    Entry(Entry&&) noexcept;
    ~Entry();

    // Adds a new |session| onto sessions, dropping the oldest one if two are
    // already stored.
    void pushSession(bssl::UniquePtr<SSL_SESSION> session);

    // Retrieves the latest session from the entry, meanwhile removing it.
    bssl::UniquePtr<SSL_SESSION> popSession();

    SSL_SESSION* peekSession();

    // We only save the last two sessions per server as that is sufficient in practice.
    bssl::UniquePtr<SSL_SESSION> sessions[2];
    std::unique_ptr<quic::TransportParameters> params;
    std::unique_ptr<quic::ApplicationState> application_state;
  };

  // Remove all entries that are no longer valid. If after that we are still at the size limit, also
  // remove the oldest entry.
  void prune();

  // Creates a new entry and insert into |cache_|.
  void createAndInsertEntry(const quic::QuicServerId& server_id,
                            bssl::UniquePtr<SSL_SESSION> session,
                            const quic::TransportParameters& params,
                            const quic::ApplicationState* application_state);

  std::map<quic::QuicServerId, Entry> cache_;
  TimeSource& time_source_;
};

} // namespace Quic
} // namespace Envoy
