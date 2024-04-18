#pragma once

#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {

// A wrapper around the actual listener filter which checks against the given filter matcher.
template <typename ListenerFilterType>
class GenericListenerFilterImplBase : public ListenerFilterType {
public:
  GenericListenerFilterImplBase(const Network::ListenerFilterMatcherSharedPtr& matcher,
                                std::unique_ptr<ListenerFilterType> listener_filter)
      : listener_filter_(std::move(listener_filter)), matcher_(std::move(matcher)) {}

  Network::FilterStatus onAccept(ListenerFilterCallbacks& cb) override {
    if (isDisabled(cb)) {
      return Network::FilterStatus::Continue;
    }
    return listener_filter_->onAccept(cb);
  }

protected:
  /**
   * Check if this listener filter should be disabled on the incoming socket.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   **/
  bool isDisabled(ListenerFilterCallbacks& cb) {
    if (matcher_ == nullptr) {
      return false;
    } else {
      return matcher_->matches(cb);
    }
  }

  const std::unique_ptr<ListenerFilterType> listener_filter_;

private:
  const Network::ListenerFilterMatcherSharedPtr matcher_;
};

} // namespace Network
} // namespace Envoy
