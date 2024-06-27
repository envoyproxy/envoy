#pragma once

#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"

namespace Envoy {
namespace Quic {

class ServerPreferredAddressConfig : public Quic::EnvoyQuicServerPreferredAddressConfig {
public:
  struct IpVersionConfig {
    quic::QuicSocketAddress spa_;
    quic::QuicIpAddress dnat_;
  };

  ServerPreferredAddressConfig(const IpVersionConfig& v4, const IpVersionConfig& v6)
      : v4_(v4), v6_(v6) {}

  Addresses getServerPreferredAddresses(
      const Network::Address::InstanceConstSharedPtr& local_address) override;

private:
  const IpVersionConfig v4_;
  const IpVersionConfig v6_;
};

} // namespace Quic
} // namespace Envoy
