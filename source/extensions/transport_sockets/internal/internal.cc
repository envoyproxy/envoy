#include "source/extensions/transport_sockets/internal/internal.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Internal {

Config::Config(const envoy::extensions::transport_sockets::internal::v3::InternalUpstreamTransport&,
               Stats::Scope&) {}

InternalSocket::InternalSocket(ConfigConstSharedPtr config,
                               Network::TransportSocketPtr inner_socket)
    : PassthroughSocket(std::move(inner_socket)), config_(std::move(config)) {}


} // namespace Internal
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
