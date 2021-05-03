#pragma once

#include "envoy/common/backoff_strategy.h"
#include "envoy/thread_local/thread_local.h"

#include "common/http/alternate_protocols_cache.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

class AlternateProtocolsCacheImpl : public AlternateProtocolsCache {
public:
  AlternateProtocolsCacheImpl(ThreadLocal::Instance& tls, TimeSource& time_source);
  ~AlternateProtocolsCacheImpl() override;

  // AlternateProtocols
  void setAlternatives(const Origin& origin, const std::vector<AlternateProtocol>& protocols,
                       const MonotonicTime& expiration) override;
  OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) override;
  size_t size() const override;

private:
  struct Entry {
    std::vector<AlternateProtocol> protocols_;
    MonotonicTime expiration_;
  };

  struct State : ThreadLocal::ThreadLocalObject {
    // Map from hostname to list of alternate protocols.
    // TODO(RyanTheOptimist): Add a limit to the size of this map and evict based on usage.
    std::map<Origin, Entry> protocols_;
  };

  // Time source used to check expiration of entries.
  TimeSource& time_source_;
  // Thread local state for the cache
  ThreadLocal::TypedSlot<State> slot_;
};

} // namespace Http
} // namespace Envoy
