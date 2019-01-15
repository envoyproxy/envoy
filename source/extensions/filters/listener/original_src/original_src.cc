#include "extensions/filters/listener/original_src/original_src.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/network/utility.h"

#include "extensions/filters/listener/original_src/original_src_socket_option.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

OriginalSrcFilter::OriginalSrcFilter(const Config& config) : config_(config) {}

Network::FilterStatus OriginalSrcFilter::onAccept(Network::ListenerFilterCallbacks& cb) {
  auto& socket = cb.socket();
  auto address = socket.remoteAddress();
  ASSERT(address);

  ENVOY_LOG(trace, "Got a new connection in the original_src filter for address {}",
            address->asString());

  if (address->type() != Network::Address::Type::Ip) {
    // nothing we can do with this.
    return Network::FilterStatus::Continue;
  }

  auto address_without_port = Network::Utility::getAddressWithPort(*address, 0);

  Network::Socket::OptionConstSharedPtr new_option =
      std::make_shared<OriginalSrcSocketOption>(std::move(address_without_port));
  // note: we don't expect this to change the behaviour of the socket. We expect it to be copied
  // into the upstream connection later.
  socket.addOption(new_option);
  return Network::FilterStatus::Continue;
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
