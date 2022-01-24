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
  MatchingDataImpl(const ConnectionSocket& socket) : socket_(socket) {}
  const ConnectionSocket& socket() const override { return socket_; }

private:
  const ConnectionSocket& socket_;
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
