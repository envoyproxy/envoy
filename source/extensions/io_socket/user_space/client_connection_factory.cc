#include "client_connection_factory.h"

#include "envoy/registry/registry.h"
//#include "envoy/singleton/manager.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

REGISTER_FACTORY(InternalClientConnectionFactory, Network::ClientConnectionFactory);
}
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
