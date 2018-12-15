#pragma once

#include "envoy/network/address.h"
#include "envoy/network/filter.h"

#include "common/common/logger.h"

#include "extensions/filters/network/original_src/config.h"
#include "extensions/filters/network/original_src/original_src_socket_option.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

/**
 * Implements the Original Src network filter. This filter places the source address of the socket
 * into an option which will alter be used to partition upstream connections.
 * This does not support non-ip (e.g. AF_UNIX) connections, which will be failed and counted.
 */
class OriginalSrcFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  OriginalSrcFilter(const Config& config);
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Config config_;
};

} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
