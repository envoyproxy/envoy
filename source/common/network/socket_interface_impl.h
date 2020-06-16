#pragma once

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Network {

class SocketInterfaceImpl : public SocketInterface {
public:
  IoHandlePtr socket(Socket::Type socket_type, Address::Type addr_type,
                     Address::IpVersion version) override;
  IoHandlePtr socket(Socket::Type socket_type, const Address::InstanceConstSharedPtr addr) override;
  IoHandlePtr socket(os_fd_t fd) override;
  bool ipFamilySupported(int domain) override;
};

using SocketInterfaceSingleton = InjectableSingleton<SocketInterface>;
using SocketInterfaceLoader = ScopedInjectableLoader<SocketInterface>;

} // namespace Network
} // namespace Envoy