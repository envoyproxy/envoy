#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/transport_socket.h"

#include "common/network/connection_impl.h"
#include "common/ssl/context_impl.h"
#include "common/ssl/ssl_socket.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

// TODO(lizan): Remove Ssl::ConnectionImpl entirely when factory of TransportSocket is ready.
class ConnectionImpl : public Network::ConnectionImpl {
public:
  ConnectionImpl(Event::Dispatcher& dispatcher, Network::ConnectionSocketPtr&& socket,
                 bool connected, Context& ctx, InitialState state);
};

} // namespace Ssl
} // namespace Envoy
