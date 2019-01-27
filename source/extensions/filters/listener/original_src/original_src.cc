#include "extensions/filters/listener/original_src/original_src.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/network/socket_option_factory.h"
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

  ENVOY_LOG(debug,
            "Got a new connection in the original_src filter for address {}. Marking with {}",
            address->asString(), config_.mark());

  if (address->type() != Network::Address::Type::Ip) {
    // nothing we can do with this.
    return Network::FilterStatus::Continue;
  }

  auto address_without_port = Network::Utility::getAddressWithPort(*address, 0);

  // note: we don't expect this to change the behaviour of the socket. We expect it to be copied
  // into the upstream connection later.
  auto options_to_add = std::make_shared<Network::Socket::Options>();
  options_to_add->emplace_back(
      std::make_shared<OriginalSrcSocketOption>(std::move(address_without_port)));

  if (config_.mark() != 0) {
    auto mark_options = Network::SocketOptionFactory::buildSocketMarkOptions(config_.mark());
    options_to_add->insert(options_to_add->end(), mark_options->begin(), mark_options->end());
  }

  auto transparent_options = Network::SocketOptionFactory::buildIpTransparentOptions();
  options_to_add->insert(options_to_add->end(), transparent_options->begin(),
                         transparent_options->end());

  socket.addOptions(options_to_add);
  return Network::FilterStatus::Continue;
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
