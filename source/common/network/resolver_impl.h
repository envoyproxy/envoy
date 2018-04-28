#pragma once

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/resolver.h"

#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {
namespace Address {
/**
 * Create an Instance from a envoy::api::v2::core::Address.
 * @param address supplies the address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr resolveProtoAddress(const envoy::api::v2::core::Address& address);

/**
 * Create an Instance from a envoy::api::v2::core::SocketAddress.
 * @param address supplies the socket address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::api::v2::core::SocketAddress& address);

/**
 * Create an InstanceRange from a envoy::api::v2::core::SocketAddressPortRange.
 * @param address_range supplies the socket address port range proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceRangeConstSharedPtr
resolveProtoSocketAddressRange(const envoy::api::v2::core::SocketAddressPortRange& address);
} // namespace Address
} // namespace Network
} // namespace Envoy
