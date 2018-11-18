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
  virtual Network::Address::InstanceConstSharedPtr getOriginalDst(int fd);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
