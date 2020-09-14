#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {

/**
 * GenericListenerFilter wraps another ListenerFilter to provides the common listener filter
 * attributes such as ListenerFilterMatcher.
 */
class GenericListenerFilter : public Network::ListenerFilter {
public:
  GenericListenerFilter(const Network::ListenerFilterMatcherSharedPtr& matcher,
                        Network::ListenerFilterPtr listener_filter)
      : listener_filter_(std::move(listener_filter)), matcher_(std::move(matcher)) {}
  Network::FilterStatus onAccept(ListenerFilterCallbacks& cb) override {
    if (isDisabled(cb)) {
      return Network::FilterStatus::Continue;
    }
    return listener_filter_->onAccept(cb);
  }
  /**
   * Check if this filter filter should be disabled on the incoming socket.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   **/
  bool isDisabled(ListenerFilterCallbacks& cb) {
    if (matcher_ == nullptr) {
      return false;
    } else {
      return matcher_->matches(cb);
    }
  }

private:
  const Network::ListenerFilterPtr listener_filter_;
  const Network::ListenerFilterMatcherSharedPtr matcher_;
};
using ListenerFilterWrapperPtr = std::unique_ptr<GenericListenerFilter>;

} // namespace Network
} // namespace Envoy