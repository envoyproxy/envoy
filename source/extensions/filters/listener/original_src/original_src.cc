#include "extensions/filters/listener/original_src/original_src.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

#include "extensions/filters/common/original_src/socket_option_factory.h"

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
  auto options_to_add =
      Filters::Common::OriginalSrc::buildOriginalSrcOptions(std::move(address), config_.mark());
  socket.addOptions(std::move(options_to_add));
  return Network::FilterStatus::Continue;
}

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
