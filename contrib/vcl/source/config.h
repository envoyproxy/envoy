#pragma once

#include "source/common/network/socket_interface.h"

#include "contrib/vcl/source/vcl_interface.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

class VclSocketInterfaceExtension : public Envoy::Network::SocketInterfaceExtension {
public:
  VclSocketInterfaceExtension(Envoy::Network::SocketInterface& sock_interface);

private:
  std::unique_ptr<Envoy::Network::SocketInterface> socket_interface_;
};

DECLARE_FACTORY(VclSocketInterface);

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy
