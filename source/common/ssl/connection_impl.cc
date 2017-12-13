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

ConnectionImpl::~ConnectionImpl() {
  // Filters may care about whether this connection is an SSL connection or not in their
  // destructors for stat reasons. We destroy the filters here vs. the base class destructors
  // to make sure they have the chance to still inspect SSL specific data via virtual functions.
  filter_manager_.destroyFilters();
}

ClientConnectionImpl::ClientConnectionImpl(Event::Dispatcher& dispatcher, Context& ctx,
                                           Network::Address::InstanceConstSharedPtr address,
                                           Network::Address::InstanceConstSharedPtr source_address)
    : ConnectionImpl(dispatcher, address->socket(Network::Address::SocketType::Stream), address,
                     nullptr, source_address, false, false, ctx, InitialState::Client) {}

void ClientConnectionImpl::connect() { doConnect(); }

} // namespace Ssl
} // namespace Envoy
