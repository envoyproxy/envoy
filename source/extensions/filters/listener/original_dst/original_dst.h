#pragma once

#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

/**
 * Implementation of an original destination listener filter.
 */
class OriginalDstFilter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  OriginalDstFilter(const envoy::config::core::v3::TrafficDirection& trafic_direction)
      : trafic_direction_(trafic_direction) {}

  virtual Network::Address::InstanceConstSharedPtr getOriginalDst(Network::Socket& sock);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  envoy::config::core::v3::TrafficDirection trafic_direction_;
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
