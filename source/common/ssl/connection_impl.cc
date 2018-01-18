#include "common/ssl/connection_impl.h"

namespace Envoy {
namespace Ssl {

ConnectionImpl::ConnectionImpl(Event::Dispatcher& dispatcher, Network::ConnectionSocketPtr&& socket,
                               bool connected, Context& ctx, InitialState state)
    : Network::ConnectionImpl(dispatcher, std::move(socket),
                              std::make_unique<SslSocket>(ctx, state), connected) {}

} // namespace Ssl
} // namespace Envoy
