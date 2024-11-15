#pragma once

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {
namespace Address {
/**
 * Create an Instance from a envoy::config::core::v3::Address.
 * @param address supplies the address proto to resolve.
 * @return pointer to the Instance or an error status
 */
absl::StatusOr<Address::InstanceConstSharedPtr>
resolveProtoAddress(const envoy::config::core::v3::Address& address);

/**
 * Create an Instance from a envoy::config::core::v3::SocketAddress.
 * @param address supplies the socket address proto to resolve.
 * @return pointer to the Instance or an error status.
 */
absl::StatusOr<Address::InstanceConstSharedPtr>
resolveProtoSocketAddress(const envoy::config::core::v3::SocketAddress& address);

DECLARE_FACTORY(IpResolver);

} // namespace Address
} // namespace Network
} // namespace Envoy
