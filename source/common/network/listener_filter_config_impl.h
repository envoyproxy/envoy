#pragma once

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Network {

class ListenerFilterConfigImpl : public Network::ListenerFilterConfig {
public:
  ListenerFilterConfigImpl(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher);
  ~ListenerFilterConfigImpl() override = default;
  ListenerFilterMatcherSharedPtr matcher() const override;

private:
  ListenerFilterMatcherSharedPtr matcher_;
};
} // namespace Network
} // namespace Envoy