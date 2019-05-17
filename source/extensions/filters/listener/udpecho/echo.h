#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace UdpEcho {

/**
 * Implementation of a basic UDP echo filter.
 */
class EchoFilter : public Network::UdpListenerReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  // Network::UdpListenerReadFilter
  void onData(Network::UdpRecvData& data) override;
  void initializeCallbacks(Network::UdpReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::UdpReadFilterCallbacks* read_callbacks_{};
};

} // namespace UdpEcho
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
