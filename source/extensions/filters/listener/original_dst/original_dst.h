#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

/**
 * Implementation of an original destination listener filter.
 */
class OriginalDstFilter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  OriginalDstFilter(const envoy::config::core::v3::TrafficDirection& traffic_direction)
      : traffic_direction_(traffic_direction) {
    // [[maybe_unused]] attribute is not supported on GCC for class members. We trivially use the
    // parameter here to silence the warning.
    (void)traffic_direction_;
  }

  virtual Network::Address::InstanceConstSharedPtr getOriginalDst(Network::Socket& sock);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  envoy::config::core::v3::TrafficDirection traffic_direction_;
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
