#pragma once

#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {
namespace Matching {

/**
 * Implementation of Network::MatchingData, providing connection-level data to
 * the match tree.
 */
class MatchingDataImpl : public MatchingData {
public:
  static absl::string_view name() { return "network"; }

  void onSocket(const ConnectionSocket& socket) { socket_ = &socket; }

  virtual const ConnectionSocket& socket() { return *socket_; }

private:
  const ConnectionSocket* socket_{};
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
