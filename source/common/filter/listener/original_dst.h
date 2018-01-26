#pragma once

#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Filter {
namespace Listener {

/**
 * Implementation of an original destination listener filter.
 */
class OriginalDst : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  virtual Network::Address::InstanceConstSharedPtr getOriginalDst(int fd);

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
};

} // namespace Listener
} // namespace Filter
} // namespace Envoy
