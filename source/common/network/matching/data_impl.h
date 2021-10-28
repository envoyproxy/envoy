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

  NetworkMatchingDataImpl(const Address::Ip* source, const Address::Ip* destination)
      : source_(source), destination_(destination) {}

  OptRef<const Address::Ip> sourceIp() const override { return makeOptRefFromPtr(source_); }

  OptRef<const Address::Ip> destinationIp() const override {
    return makeOptRefFromPtr(destination_);
  }

  absl::optional<uint32_t> sourcePort() const override {
    if (source_) {
      return source_->port();
    }

    return absl::nullopt;
  }

  absl::optional<uint32_t> destinationPort() const override {
    if (destination_) {
      return destination_->port();
    }

    return absl::nullopt;
  }

private:
  const Address::Ip* const source_{};
  const Address::Ip* const destination_{};
};
} // namespace Matching
} // namespace Network
} // namespace Envoy
