#include "source/common/quic/envoy_quic_session_cache.h"

namespace Envoy {
namespace Quic {
namespace {

// This value was chosen arbitrarily. We can make this configurable if needed.
// TODO(14829) ensure this is tested and scaled for upstream.
constexpr size_t MaxSessionCacheEntries = 1024;

// Returns false if the SSL session doesn't exist or it is expired.
bool isSessionValid(SSL_SESSION* session, SystemTime now) {
  if (session == nullptr) {
    return false;
  }
  const time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  if (now_time_t < 0) {
    return false;
  }
  const uint64_t now_u64 = static_cast<uint64_t>(now_time_t);
  const uint64_t session_time = SSL_SESSION_get_time(session);
  const uint64_t session_expiration = session_time + SSL_SESSION_get_timeout(session);
  // now_u64 may be slightly behind because of differences in how time is calculated at this layer
  // versus BoringSSL. Add a second of wiggle room to account for this.
  return session_time <= now_u64 + 1 && now_u64 < session_expiration;
}

bool doApplicationStatesMatch(const quic::ApplicationState* state,
                              const quic::ApplicationState* other) {
  if (state == other) {
    return true;
  }
  if ((state != nullptr && other == nullptr) || (state == nullptr && other != nullptr)) {
    return false;
  }
  return (*state == *other);
}

} // namespace

EnvoyQuicSessionCache::EnvoyQuicSessionCache(TimeSource& time_source) : time_source_(time_source) {}

EnvoyQuicSessionCache::~EnvoyQuicSessionCache() = default;

void EnvoyQuicSessionCache::Insert(const quic::QuicServerId& server_id,
                                   bssl::UniquePtr<SSL_SESSION> session,
                                   const quic::TransportParameters& params,
                                   const quic::ApplicationState* application_state) {
  auto it = cache_.find(server_id);
  if (it == cache_.end()) {
    // New server ID, add a new entry.
    createAndInsertEntry(server_id, std::move(session), params, application_state);
    return;
  }
  Entry& entry = it->second;
  if (params == *entry.params &&
      doApplicationStatesMatch(application_state, entry.application_state.get())) {
    // The states are both the same, so we only need to insert the session.
    entry.pushSession(std::move(session));
    return;
  }
  // States are different, replace the entry.
  cache_.erase(it);
  createAndInsertEntry(server_id, std::move(session), params, application_state);
}

std::unique_ptr<quic::QuicResumptionState>
EnvoyQuicSessionCache::Lookup(const quic::QuicServerId& server_id, const SSL_CTX* /*ctx*/) {
  auto it = cache_.find(server_id);
  if (it == cache_.end()) {
    return nullptr;
  }
  Entry& entry = it->second;
  const SystemTime system_time = time_source_.systemTime();
  if (!isSessionValid(entry.peekSession(), system_time)) {
    cache_.erase(it);
    return nullptr;
  }
  auto state = std::make_unique<quic::QuicResumptionState>();
  state->tls_session = entry.popSession();
  if (entry.params != nullptr) {
    state->transport_params = std::make_unique<quic::TransportParameters>(*entry.params);
  }
  if (entry.application_state != nullptr) {
    state->application_state = std::make_unique<quic::ApplicationState>(*entry.application_state);
  }
  return state;
}

void EnvoyQuicSessionCache::ClearEarlyData(const quic::QuicServerId& server_id) {
  auto it = cache_.find(server_id);
  if (it == cache_.end()) {
    return;
  }
  for (bssl::UniquePtr<SSL_SESSION>& session : it->second.sessions) {
    if (session != nullptr) {
      session.reset(SSL_SESSION_copy_without_early_data(session.get()));
    }
  }
}

void EnvoyQuicSessionCache::prune() {
  quic::QuicServerId oldest_id;
  uint64_t oldest_expiration = std::numeric_limits<uint64_t>::max();
  const SystemTime system_time = time_source_.systemTime();
  auto it = cache_.begin();
  while (it != cache_.end()) {
    Entry& entry = it->second;
    SSL_SESSION* session = entry.peekSession();
    if (!isSessionValid(session, system_time)) {
      it = cache_.erase(it);
    } else {
      if (cache_.size() >= MaxSessionCacheEntries) {
        // Only track the oldest session if we are at the size limit.
        const uint64_t session_expiration =
            SSL_SESSION_get_time(session) + SSL_SESSION_get_timeout(session);
        if (session_expiration < oldest_expiration) {
          oldest_expiration = session_expiration;
          oldest_id = it->first;
        }
      }
      ++it;
    }
  }
  if (cache_.size() >= MaxSessionCacheEntries) {
    cache_.erase(oldest_id);
  }
}

void EnvoyQuicSessionCache::createAndInsertEntry(const quic::QuicServerId& server_id,
                                                 bssl::UniquePtr<SSL_SESSION> session,
                                                 const quic::TransportParameters& params,
                                                 const quic::ApplicationState* application_state) {
  prune();
  ASSERT(cache_.size() < MaxSessionCacheEntries);
  Entry entry;
  entry.pushSession(std::move(session));
  entry.params = std::make_unique<quic::TransportParameters>(params);
  if (application_state != nullptr) {
    entry.application_state = std::make_unique<quic::ApplicationState>(*application_state);
  }
  cache_.insert(std::make_pair(server_id, std::move(entry)));
}

size_t EnvoyQuicSessionCache::size() const { return cache_.size(); }

EnvoyQuicSessionCache::Entry::Entry() = default;
EnvoyQuicSessionCache::Entry::Entry(Entry&&) noexcept = default;
EnvoyQuicSessionCache::Entry::~Entry() = default;

void EnvoyQuicSessionCache::Entry::pushSession(bssl::UniquePtr<SSL_SESSION> session) {
  if (sessions[0] != nullptr) {
    sessions[1] = std::move(sessions[0]);
  }
  sessions[0] = std::move(session);
}

bssl::UniquePtr<SSL_SESSION> EnvoyQuicSessionCache::Entry::popSession() {
  if (sessions[0] == nullptr) {
    return nullptr;
  }
  bssl::UniquePtr<SSL_SESSION> session = std::move(sessions[0]);
  sessions[0] = std::move(sessions[1]);
  sessions[1] = nullptr;
  return session;
}

SSL_SESSION* EnvoyQuicSessionCache::Entry::peekSession() { return sessions[0].get(); }

} // namespace Quic
} // namespace Envoy
