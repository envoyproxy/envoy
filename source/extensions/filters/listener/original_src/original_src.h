#pragma once

#include "envoy/network/address.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/listener/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

/**
 * Implements the Original Src network filter. This filter places the source address of the socket
 * into an option which will alter be used to partition upstream connections.
 * This does not support non-ip (e.g. AF_UNIX) connections, which will be failed and counted.
 */
class OriginalSrcFilter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  OriginalSrcFilter(const Config& config);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

  size_t maxReadBytes() const override { return 0; }

  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  };

private:
  Config config_;
};

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
