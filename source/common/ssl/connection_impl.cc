#include "common/ssl/connection_impl.h"

namespace Envoy {
namespace Ssl {

ConnectionImpl::ConnectionImpl(Event::Dispatcher& dispatcher, int fd,
                               Network::Address::InstanceConstSharedPtr remote_address,
                               Network::Address::InstanceConstSharedPtr local_address,
                               Network::Address::InstanceConstSharedPtr bind_to_address,
                               bool using_original_dst, bool connected, Context& ctx,
                               InitialState state)
    : Network::ConnectionImpl(dispatcher, fd, remote_address, local_address, bind_to_address,
                              Network::TransportSocketPtr{new SslSocket(ctx, state)},
                              using_original_dst, connected) {}

} // namespace Ssl
} // namespace Envoy
