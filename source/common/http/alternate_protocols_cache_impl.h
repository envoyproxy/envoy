#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/key_value_store.h"
#include "envoy/common/optref.h"
#include "envoy/common/time.h"
#include "envoy/http/alternate_protocols_cache.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// An implementation of AlternateProtocolsCache.
// See: source/docs/http3_upstream.md
class AlternateProtocolsCacheImpl : public AlternateProtocolsCache {
public:
  AlternateProtocolsCacheImpl(TimeSource& time_source, std::unique_ptr<KeyValueStore>&& store);
  ~AlternateProtocolsCacheImpl() override;

  // Convert an AlternateProtocol vector to a string to cache to the key value
  // store. Note that in order to determine the lifetime of entries, this
  // function will serialize ma= as absolute time from the epoch rather than
  // relative time.
  // This function also does not do standards-required normalization. Entries requiring
  // normalization will simply not be read from cache.
  static std::string protocolsToStringForCache(const std::vector<AlternateProtocol>& protocols,
                                               TimeSource& time_source);
  // Parse an alternate protocols string into structured data, or absl::nullopt
  // if it is empty or invalid.
  // If from_cache is true, it is assumed the string was serialized using
  // protocolsToStringForCache and the the ma fields will be parsed as absolute times
  // rather than relative time.
  static absl::optional<std::vector<AlternateProtocol>>
  protocolsFromString(absl::string_view protocols, TimeSource& time_source,
                      bool from_cache = false);

  // AlternateProtocolsCache
  void setAlternatives(const Origin& origin, std::vector<AlternateProtocol>& protocols) override;
  OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) override;
  size_t size() const override;

private:
  // Time source used to check expiration of entries.
  TimeSource& time_source_;

  // Map from hostname to list of alternate protocols.
  // TODO(RyanTheOptimist): Add a limit to the size of this map and evict based on usage.
  std::map<Origin, std::vector<AlternateProtocol>> protocols_;

  // The key value store, if flushing to persistent storage.
  std::unique_ptr<KeyValueStore> key_value_store_;
};

} // namespace Http
} // namespace Envoy
