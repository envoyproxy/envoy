#pragma once

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/resolver.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {
namespace Address {
/**
 * Create an Instance from a envoy::config::core::v3::Address.
 * @param address supplies the address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr
resolveProtoAddress(const envoy::config::core::v3::Address& address);

/**
 * Create an Instance from a envoy::config::core::v3::SocketAddress.
 * @param address supplies the socket address proto to resolve.
 * @return pointer to the Instance.
 */
Address::InstanceConstSharedPtr
resolveProtoSocketAddress(const envoy::config::core::v3::SocketAddress& address);
} // namespace Address
} // namespace Network
} // namespace Envoy
