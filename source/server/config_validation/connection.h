#include "common/network/connection_impl.h"

namespace Envoy {
namespace Network {

/*
  Class representing connection to upstream entity in config verification mode. The connection is
  not really established, but sockets may be allocated. Methods doing "real" connections should be
  overridden with no-op implementations.
*/
class ConfigValidateConnection : public Network::ClientConnectionImpl {
public:
  ConfigValidateConnection(Event::ValidationDispatcher& dispatcher,
                           Network::Address::InstanceConstSharedPtr remote_address,
                           Network::Address::InstanceConstSharedPtr source_address,
                           Network::TransportSocketPtr&& transport_socket,
                           const Network::ConnectionSocket::OptionsSharedPtr& options)
      : Network::ClientConnectionImpl(dispatcher, remote_address, source_address,
                                      std::move(transport_socket), options) {}

  // Unit tests may instantiate it without proper event machine and leave opened sockets.
  // Do some cleanup before invoking base class's destructor.
  virtual ~ConfigValidateConnection() { close(ConnectionCloseType::NoFlush); }
};

} // namespace Network
} // namespace Envoy
