#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/transport_socket.h"

namespace Envoy {
namespace Network {

void voo() {
  Event::MockDispatcher dispatcher_;
  MockTransportSocketFactory transport_socket_factory;
  Network::TransportSocketOptionsSharedPtr transport_socket_options;
      const Network::ConnectionSocket::OptionsSharedPtr options;


  HappyEyeballsConnectionImpl impl_(dispatcher_,
                                    Network::Address::InstanceConstSharedPtr(),
                                    Network::Address::InstanceConstSharedPtr(),
                                    transport_socket_factory,
                                    transport_socket_options,
                                    options);
}



} // namespace Network
} // namespace Envoy
