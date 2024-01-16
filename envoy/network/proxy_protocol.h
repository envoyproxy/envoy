#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

struct ProxyProtocolTLV {
  const uint8_t type;
  const std::vector<unsigned char> value;
};

using ProxyProtocolTLVVector = std::vector<ProxyProtocolTLV>;

struct ProxyProtocolData {
  const Network::Address::InstanceConstSharedPtr src_addr_;
  const Network::Address::InstanceConstSharedPtr dst_addr_;
  const ProxyProtocolTLVVector tlv_vector_{};
  std::string asStringForHash() const {
    return std::string(src_addr_ ? src_addr_->asString() : "null") +
           (dst_addr_ ? dst_addr_->asString() : "null");
  }
};

} // namespace Network
} // namespace Envoy
