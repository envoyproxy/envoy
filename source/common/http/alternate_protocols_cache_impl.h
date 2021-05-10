#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/http/alternate_protocols_cache.h"
#include "envoy/common/optref.h"
#include "envoy/common/time.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// Tracks alternate protocols that can be used to make an HTTP connection to an origin server.
// See https://tools.ietf.org/html/rfc7838 for HTTP Alternate Services and
// https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04 for the
// "HTTPS" DNS resource record.
class AlternateProtocolsCacheImpl : public AlternateProtocolsCache {
public:
  explicit AlternateProtocolsCacheImpl(TimeSource& time_source);
  ~AlternateProtocolsCacheImpl() override;

  // Sets the possible alternative protocols which can be used to connect to the
  // specified origin. Expires after the specified expiration time.
  void setAlternatives(const Origin& origin, const std::vector<AlternateProtocol>& protocols,
                       const MonotonicTime& expiration) override;

  // Returns the possible alternative protocols which can be used to connect to the
  // specified origin, or nullptr if not alternatives are found. The returned pointer
  // is owned by the AlternateProtocolsCacheImpl and is valid until the next operation on
  // AlternateProtocolsCacheImpl.
  OptRef<const std::vector<AlternateProtocol>> findAlternatives(const Origin& origin) override;

  // Returns the number of entries in the map.
  size_t size() const override;

private:
  struct Entry {
    std::vector<AlternateProtocol> protocols_;
    MonotonicTime expiration_;
  };

  // Time source used to check expiration of entries.
  TimeSource& time_source_;

  // Map from hostname to list of alternate protocols.
  // TODO(RyanTheOptimist): Add a limit to the size of this map and evict based on usage.
  std::map<Origin, Entry> protocols_;
};

} // namespace Http
} // namespace Envoy
