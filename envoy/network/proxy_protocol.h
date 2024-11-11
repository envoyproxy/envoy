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

enum class ProxyProtocolVersion {
  // The proxy-protocol filter is not present in the filter chain and another filter has set
  // the proxy-protocol version.
  NotUsed = 0,
  // The proxy-protocol filter did not find any proxy-protocol header and was skipped.
  NotFound = 1,
  V1 = 2,
  V2 = 3
};

struct ProxyProtocolDataWithVersion : public ProxyProtocolData {
  const ProxyProtocolVersion version_;
};
} // namespace Network
} // namespace Envoy
