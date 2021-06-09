#pragma once

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

struct ProxyProtocolData {
  const Network::Address::InstanceConstSharedPtr src_addr_;
  const Network::Address::InstanceConstSharedPtr dst_addr_;
  std::string asStringForHash() const {
    return std::string(src_addr_ ? src_addr_->asString() : "null") +
           (dst_addr_ ? dst_addr_->asString() : "null");
  }
};

} // namespace Network
} // namespace Envoy
