#pragma once

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Network {

/**
 * A non-empty listener filter matcher which aggregates multiple sub matchers.
 */
class SetListenerFilterMatcher : public ListenerFilterMatcher {
public:
  explicit SetListenerFilterMatcher(
      const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config);

  bool matches(ListenerFilterCallbacks& cb) const override { return matchers_[0]->matches(cb); }

private:
  std::vector<ListenerFilterMatcherPtr> matchers_;
};
} // namespace Network
} // namespace Envoy