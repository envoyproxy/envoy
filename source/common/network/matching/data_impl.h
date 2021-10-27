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

  void onSourceIp(const Address::Ip& source_ip) { source_ip_ = &source_ip; }

  void onDestinationIp(const Address::Ip& destination_ip) { destination_ip_ = &destination_ip; }

  void onSourcePort(uint16_t source_port) { source_port_ = source_port; }

  void onDestinationPort(uint16_t destination_port) { destination_port_ = destination_port; }

  OptRef<const Address::Ip> sourceIp() const override { return makeOptRefFromPtr(source_ip_); }

  OptRef<const Address::Ip> destinationIp() const override {
    return makeOptRefFromPtr(destination_ip_);
  }

  absl::optional<uint16_t> sourcePort() const override { return source_port_; }

  absl::optional<uint16_t> destinationPort() const override { return destination_port_; }

private:
  const Address::Ip* source_ip_{};
  const Address::Ip* destination_ip_{};
  absl::optional<uint16_t> source_port_{};
  absl::optional<uint16_t> destination_port_{};
};
} // namespace Matching
} // namespace Network
} // namespace Envoy
