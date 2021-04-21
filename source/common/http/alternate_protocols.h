#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/time.h"

namespace Envoy {
namespace Http {

// Tracks alternate protocols that can be used to make an HTTP connection to an origin server.
class AlternateProtocols {
public:
  // Represents an HTTP origin to be connected too.
  struct Origin {
  public:
    Origin(std::string scheme, std::string hostname, int port);

    bool operator==(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) ==
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    bool operator!=(const Origin& other) const { return !this->operator==(other); }

    bool operator<(const Origin& other) const {
      return std::tie(scheme_, hostname_, port_) <
             std::tie(other.scheme_, other.hostname_, other.port_);
    }

    std::string scheme_;
    std::string hostname_;
    int port_{};
  };

  // Represents an alternative protocol that can be used to connect to an origin.
  struct AlternateProtocol {
  public:
    AlternateProtocol(std::string alpn, std::string hostname, int port);

    bool operator==(const AlternateProtocol& other) const {
      return std::tie(alpn_, hostname_, port_) ==
             std::tie(other.alpn_, other.hostname_, other.port_);
    }

    std::string alpn_;
    std::string hostname_;
    int port_;
  };

  AlternateProtocols(TimeSource& time_source);

  // Sets the possible alternative protocols which can be used to connect to the
  // specified origin. Expires after the specified expiration time.
  void setAlternatives(const Origin& origin, const std::vector<AlternateProtocol>& protocols,
                       const MonotonicTime& expiration);

  // Returns the possible alternative protocols which can be used to connect to the
  // specified origin, or nullptr if not alternatives are found.
  const std::vector<AlternateProtocol>* findAlternatives(const Origin& origin);

  // Returns the number of entries in the map.
  size_t size() const;

private:
  struct Entry {
  public:
    std::vector<AlternateProtocol> protocols_;
    MonotonicTime expiration_;
  };

  // Time source used to check expiration of entries.
  TimeSource& time_source_;

  // Map from hostname to list of alternate protocols.
  // TODO(RyanTheOptimist): Add a limit to size of this map and evict based on usage.
  std::map<Origin, Entry> protocols_;
};

} // namespace Http
} // namespace Envoy
