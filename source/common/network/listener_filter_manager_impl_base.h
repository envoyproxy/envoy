#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {

class ListenerFilterManagerImplBase : public ListenerFilterManager {
public:
  // Network::ListenerFilterManager
  void addAcceptFilter(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                       Network::ListenerFilterPtr&& filter) override;

  virtual void startFilterChain() PURE;

protected:
  std::list<Network::ListenerFilterPtr> accept_filters_;
};

} // namespace Network
} // namespace Envoy
