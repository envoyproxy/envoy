#pragma once

#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {
namespace Matching {
/**
 * Implementation of NetworkMatchingData, providing network specific data to
 * the match tree.
 */
class NetworkMatchingDataImpl : public NetworkMatchingData {
public:
  static absl::string_view name() { return "network"; }

  NetworkMatchingDataImpl(const Address::Ip* source) : source_(source) {}

  OptRef<const Address::Ip> sourceIp() const override { return makeOptRefFromPtr(source_); }

private:
  const Address::Ip* const source_{};
};
} // namespace Matching
} // namespace Network
} // namespace Envoy
