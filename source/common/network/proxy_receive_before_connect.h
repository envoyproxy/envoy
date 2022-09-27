#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Keep downstream read enabled before connecting upstream. This is expected to be checked for by
 * any proxy (e.g., tcp_proxy) that can buffer received downstream data to be written upstream once
 * the upstream connection has been established.
 *
 * Any filter in the filter chain can set this to 'true' to request access to data received before
 * the upstream connection has been established. To prevent other filters disabling this, StateType
 * 'ReadOnly' should be used. LifeSpan of this object is Connection. No sharing with the upstream
 * should be needed.
 *
 * Note that this state needs to be set in filter's initializeReadFilterCallbacks() as tcp_proxy
 * checks for this in its initializeReadFilterCallbacks() that is called when the filter chain is
 * created, but after all the other filters as tcp_proxy is the last filter in the chain.
 */
class ProxyReceiveBeforeConnect : public StreamInfo::FilterState::Object {
public:
  ProxyReceiveBeforeConnect(bool enable) : receive_before_connect_(enable) {}
  bool value() const { return receive_before_connect_; }
  static const std::string& key();

private:
  const bool receive_before_connect_{};
};

} // namespace Network
} // namespace Envoy
