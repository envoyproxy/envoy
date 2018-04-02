#include "extensions/filters/listener/original_dst/original_dst.h"

#include "envoy/network/listen_socket.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

Network::Address::InstanceConstSharedPtr OriginalDstFilter::getOriginalDst(int fd) {
  return Network::Utility::getOriginalDst(fd);
}

Network::FilterStatus OriginalDstFilter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "original_dst: New connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  const Network::Address::Instance& local_address = *socket.localAddress();

  if (local_address.type() == Network::Address::Type::Ip) {
    Network::Address::InstanceConstSharedPtr original_local_address = getOriginalDst(socket.fd());

    // A listener that has the use_original_dst flag set to true can still receive
    // connections that are NOT redirected using iptables. If a connection was not redirected,
    // the address returned by getOriginalDst() matches the local address of the new socket.
    // In this case the listener handles the connection directly and does not hand it off.
    if (original_local_address) {
      // Restore the local address to the original one.
      socket.setLocalAddress(original_local_address, true);
    }
  }

  return Network::FilterStatus::Continue;
}

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
