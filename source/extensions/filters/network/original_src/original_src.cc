#include "extensions/filters/network/original_src/original_src.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

#include "extensions/filters/network/original_src/original_src_socket_option.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

OriginalSrcFilter::OriginalSrcFilter(const Config& config) : config_(config) {}
Network::FilterStatus OriginalSrcFilter::onData(Buffer::Instance&, bool) {
  return Network::FilterStatus::Continue;
}

Network::FilterStatus OriginalSrcFilter::onNewConnection() {
  if (!read_callbacks_) {
    return Network::FilterStatus::Continue;
  }

  auto address = read_callbacks_->connection().remoteAddress();
  ASSERT(address);

  ENVOY_LOG(trace, "Got a new connection in the original_src filter for address {}",
            address->asString());

  if (address->type() != Network::Address::Type::Ip) {
    // nothing we can do with this.
    return Network::FilterStatus::Continue;
  }

  Network::Socket::OptionConstSharedPtr new_option =
      std::make_shared<Network::OriginalSrcSocketOption>(std::move(address));
  // note: we don't expect this to change the behaviour of the socket. We expect it to be copied
  // into the upstream connection later.
  read_callbacks_->connection().addSocketOption(std::move(new_option));
  return Network::FilterStatus::Continue;
}

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
