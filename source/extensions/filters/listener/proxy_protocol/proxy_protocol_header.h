#pragma once

#include "envoy/network/address.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

struct WireHeader {
  WireHeader(size_t extensions_length)
      : extensions_length_(extensions_length), protocol_version_(Network::Address::IpVersion::v4),
        remote_address_(nullptr), local_address_(nullptr), local_command_(true) {}
  WireHeader(size_t extensions_length, Network::Address::IpVersion protocol_version,
             Network::Address::InstanceConstSharedPtr remote_address,
             Network::Address::InstanceConstSharedPtr local_address)
      : extensions_length_(extensions_length), protocol_version_(protocol_version),
        remote_address_(remote_address), local_address_(local_address), local_command_(false) {

    ASSERT(extensions_length_ <= 65535);
  }
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
