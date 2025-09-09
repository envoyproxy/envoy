#pragma once

#include "envoy/server/transport_socket_config.h"

#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using TransportSocketFactoryContextImpl = GenericFactoryContextImpl;
using TransportSocketFactoryContextImplPtr = std::unique_ptr<TransportSocketFactoryContextImpl>;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
