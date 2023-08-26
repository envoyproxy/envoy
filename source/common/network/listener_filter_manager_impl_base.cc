#include "source/common/network/listener_filter_manager_impl_base.h"

namespace Envoy {
namespace Network {

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

  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override {
    return listener_filter_->onData(buffer);
  }

  size_t maxReadBytes() const override { return listener_filter_->maxReadBytes(); }

private:
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

  const Network::ListenerFilterPtr listener_filter_;
  const Network::ListenerFilterMatcherSharedPtr matcher_;
};

void ListenerFilterManagerImplBase::addAcceptFilter(
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    Network::ListenerFilterPtr&& filter) {
  accept_filters_.emplace_back(
      std::make_unique<GenericListenerFilter>(listener_filter_matcher, std::move(filter)));
}

} // namespace Network
} // namespace Envoy
