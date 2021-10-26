#pragma once

#include "source/common/network/matching/data.h"

namespace Envoy {
namespace Network {
namespace Matching {
class NetworkMatchingDataImpl : public NetworkMatchingData {
public:
  static absl::string_view name() { return "network"; }

  void onSourceIp(const Address::CidrRange& source_ip) { source_ip_ = source_ip; }

  void onDestinationIp(const Address::CidrRange& destination_ip) {
    destination_ip_ = destination_ip;
  }

  const Address::CidrRange& sourceIp() const override { return source_ip_; }

  const Address::CidrRange& destinationIp() const override { return destination_ip_; }

private:
  Address::CidrRange source_ip_;
  Address::CidrRange destination_ip_;
};
} // namespace Matching
} // namespace Network
} // namespace Envoy
