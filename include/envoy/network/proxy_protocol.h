#pragma once

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

struct ProxyProtocolData {
  const Network::Address::InstanceConstSharedPtr src_addr_;
  const Network::Address::InstanceConstSharedPtr dst_addr_;
};

} // namespace Network
} // namespace Envoy