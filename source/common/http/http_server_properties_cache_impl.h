#pragma once

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/key_value_store.h"
#include "envoy/common/optref.h"
#include "envoy/common/time.h"
#include "envoy/http/http_server_properties_cache.h"

#include "source/common/common/logger.h"
#include "source/common/http/http3_status_tracker_impl.h"

#include "absl/strings/string_view.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace Envoy {
namespace Http {

// A cache of HTTP server properties.
// This caches
//   - Alternate protocol entries as documented here: source/docs/http3_upstream.md.
//   - QUIC round trip time used for TCP failover.
//   - The last connectivity status of HTTP/3, if available.
//   - Expected concurrent streams allowed.
class HttpServerPropertiesCacheImpl : public HttpServerPropertiesCache,
                                      Logger::Loggable<Logger::Id::alternate_protocols_cache> {
public:
  HttpServerPropertiesCacheImpl(Event::Dispatcher& dispatcher,
                                std::vector<std::string>&& canonical_suffixes,
                                std::unique_ptr<KeyValueStore>&& store, size_t max_entries);
  ~HttpServerPropertiesCacheImpl() override;

  // Captures the data tracked per origin;,
  struct OriginData {
    OriginData() = default;
    OriginData(OptRef<std::vector<AlternateProtocol>> protocols, std::chrono::microseconds srtt,
               Http3StatusTrackerPtr&& tracker, uint32_t concurrent_streams)
        : protocols(protocols), srtt(srtt), h3_status_tracker(std::move(tracker)),
          concurrent_streams(concurrent_streams) {}

    // The alternate protocols supported if available.
    absl::optional<std::vector<AlternateProtocol>> protocols;
    // The last smoothed round trip time, if available else 0.
    std::chrono::microseconds srtt;
    // The last connectivity status of HTTP/3, if available else nullptr.
    Http3StatusTrackerPtr h3_status_tracker;
    // The number of concurrent streams expected to be allowed.
    uint32_t concurrent_streams;
  };

  // Converts an Origin to a string which can be parsed by stringToOrigin.
  static std::string originToString(const HttpServerPropertiesCache::Origin& origin);
  // Converts a string from originToString back to structured format.
  static absl::optional<HttpServerPropertiesCache::Origin> stringToOrigin(const std::string& str);

  // Convert origin data to a string to cache to the key value
  // store. Note that in order to determine the lifetime of entries, this
  // function will serialize ma= as absolute time from the epoch rather than
  // relative time.
  // This function also does not do standards-required normalization. Entries requiring
  // normalization will simply not be read from cache.
  // The string format is:
  // protocols|rtt
  static std::string originDataToStringForCache(const OriginData& data);
  // Parse an origin data into structured data, or absl::nullopt
  // if it is empty or invalid.
  // If from_cache is true, it is assumed the string was serialized using
  // protocolsToStringForCache and the the ma fields will be parsed as absolute times
  // rather than relative time.
  static absl::optional<OriginData> originDataFromString(absl::string_view origin_data,
                                                         TimeSource& time_source, bool from_cache);
  // Parse an alt-svc string into a vector of structured data.
  // If from_cache is true, it is assumed the string was serialized using
  // protocolsToStringForCache and the the ma fields will be parsed as absolute times
  // rather than relative time.
  static std::vector<Http::HttpServerPropertiesCache::AlternateProtocol>
  alternateProtocolsFromString(absl::string_view altsvc_str, TimeSource& time_source,
                               bool from_cache);

  // HttpServerPropertiesCache
  void setAlternatives(const Origin& origin, std::vector<AlternateProtocol>& protocols) override;
  void setSrtt(const Origin& origin, std::chrono::microseconds srtt) override;
  std::chrono::microseconds getSrtt(const Origin& origin) const override;
  void setConcurrentStreams(const Origin& origin, uint32_t concurrent_streams) override;
  uint32_t getConcurrentStreams(const Origin& origin) const override;
  OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) override;
  size_t size() const override;
  HttpServerPropertiesCache::Http3StatusTracker&
  getOrCreateHttp3StatusTracker(const Origin& origin) override;

private:
  // Time source used to check expiration of entries.
  Event::Dispatcher& dispatcher_;

  struct OriginHash {
    size_t operator()(const Origin& origin) const {
      // Multiply the hashes by the magic number 37 to spread the bits around.
      size_t hash = std::hash<std::string>()(origin.scheme_) +
                    37 * (std::hash<std::string>()(origin.hostname_) +
                          37 * std::hash<uint32_t>()(origin.port_));
      return hash;
    }
  };

  using ProtocolsMap = quiche::QuicheLinkedHashMap<Origin, OriginData, OriginHash>;
  // Map from origin to list of alternate protocols.
  ProtocolsMap protocols_;

  // This allows calling setPropertiesImpl without creating an additional copy
  // of the protocols vector.
  struct OriginDataWithOptRef {
    OriginDataWithOptRef() : srtt(std::chrono::milliseconds(0)) {}
    OriginDataWithOptRef(OptRef<std::vector<AlternateProtocol>> protocols,
                         std::chrono::microseconds srtt, Http3StatusTrackerPtr&& h3_status_tracker,
                         uint32_t concurrent_streams)
        : protocols(protocols), srtt(srtt), h3_status_tracker(std::move(h3_status_tracker)),
          concurrent_streams(concurrent_streams) {}
    // The alternate protocols supported if available.
    OptRef<std::vector<AlternateProtocol>> protocols;
    // The last smoothed round trip time, if available else 0.
    std::chrono::microseconds srtt;
    // The last connectivity status of HTTP/3, if available else nullptr.
    Http3StatusTrackerPtr h3_status_tracker;
    // The number of concurrent streams expected to be allowed.
    uint32_t concurrent_streams{0};
  };

  ProtocolsMap::iterator setPropertiesImpl(const Origin& origin, OriginDataWithOptRef& origin_data);

  ProtocolsMap::iterator addOriginData(const Origin& origin, OriginData&& origin_data);

  // Returns the canonical suffix, if any, associated with `hostname`.
  absl::string_view getCanonicalSuffix(absl::string_view hostname);

  // Returns the canonical origin, if any, associated with `hostname`.
  absl::optional<Origin> getCanonicalOrigin(absl::string_view hostname);

  // If `origin` matches a canonical suffix then updates canonical_alt_svc_map_ accordingly.
  void maybeSetCanonicalOrigin(const Origin& origin);

  // The key value store, if flushing to persistent storage.
  std::unique_ptr<KeyValueStore> key_value_store_;

  // Contains a map of servers which could share the same alternate protocol.
  // Map from a Canonical suffix to an actual origin, which has a plausible alternate
  // protocol mapping.
  std::map<std::string, Origin> canonical_alt_svc_map_;

  // Contains list of suffixes (for example ".c.youtube.com",
  // ".googlevideo.com", ".googleusercontent.com") of canonical hostnames.
  std::vector<std::string> canonical_suffixes_;

  const size_t max_entries_;
};

} // namespace Http
} // namespace Envoy
