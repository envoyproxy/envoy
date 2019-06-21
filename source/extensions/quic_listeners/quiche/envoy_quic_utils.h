#include <sys/socket.h>

#include "common/network/address_impl.h"

#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

inline Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address) {
  return quic_address.IsInitialized()
             ? Network::Address::addressFromSockAddr(quic_address.generic_address(),
                                                     sizeof(sockaddr_storage), false)
             : nullptr;
}

} // namespace Quic
} // namespace Envoy
