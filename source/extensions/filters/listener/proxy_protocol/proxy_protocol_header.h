#pragma once

#include "envoy/network/address.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

struct WireHeader {
  WireHeader(size_t header_length, size_t header_addr_length, size_t addr_lengh,
             size_t extensions_length)
      : header_length_(header_length), header_addr_length_(header_addr_length),
        addr_lengh_(addr_lengh), extensions_length_(extensions_length),
        protocol_version_(Network::Address::IpVersion::v4), remote_address_(nullptr),
        local_address_(nullptr), local_command_(true) {}
  WireHeader(size_t header_length, size_t header_addr_length, size_t addr_lengh,
             size_t extensions_length, Network::Address::IpVersion protocol_version,
             Network::Address::InstanceConstSharedPtr remote_address,
             Network::Address::InstanceConstSharedPtr local_address)
      : header_length_(header_length), header_addr_length_(header_addr_length),
        addr_lengh_(addr_lengh), extensions_length_(extensions_length),
        protocol_version_(protocol_version), remote_address_(remote_address),
        local_address_(local_address), local_command_(false) {

    ASSERT(extensions_length_ <= 65535);
  }

  size_t wholeHeaderLength() { return header_length_ + header_addr_length_; }

  size_t headerLengthWithoutExtension() { return header_length_ + addr_lengh_; }

  // For v1, this is whole length of the header util the end `\r\n`.
  // For v2, this is PROXY_PROTO_V2_HEADER_LEN, without address and extensions;
  size_t header_length_;
  // For v1, this is zero. For v2, this is the length of address and extensions;
  size_t header_addr_length_;
  // For v1, this is zero. For v2, this is the length of address.
  size_t addr_lengh_;
  // For v1, this is zero, For v2, this is the length of extensions.
  size_t extensions_length_;
  const Network::Address::IpVersion protocol_version_;
  const Network::Address::InstanceConstSharedPtr remote_address_;
  const Network::Address::InstanceConstSharedPtr local_address_;
  const bool local_command_;
};

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
