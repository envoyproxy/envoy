#pragma once

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

class ServerPreferredAddressConfig : public Quic::EnvoyQuicServerPreferredAddressConfig {
public:
  struct FamilyAddresses {
    quic::QuicSocketAddress spa_;
    quic::QuicIpAddress dnat_;
  };

  ServerPreferredAddressConfig(const FamilyAddresses& v4, const FamilyAddresses& v6)
      : v4_(v4), v6_(v6) {}

  Addresses getServerPreferredAddresses(
      const Network::Address::InstanceConstSharedPtr& local_address) override;

private:
  const FamilyAddresses v4_;
  const FamilyAddresses v6_;
};

} // namespace Quic
} // namespace Envoy
