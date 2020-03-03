#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Server {

class ListenerFilterMatcher {
public:
  virtual ~ListenerFilterMatcher() = default;
  virtual bool matches(Network::ListenerFilterCallbacks& cb) const;
};
using ListenerFilterMatcherPtr = std::unique_ptr<ListenerFilterMatcher>;

struct OwnedListenerFilterMatcher : public ListenerFilterMatcher {
public:
  explicit OwnedListenerFilterMatcher(std::vector<ListenerFilterMatcherPtr> matchers)
      : matchers_(std::move(matchers)) {}

  bool matches(Network::ListenerFilterCallbacks& cb) const override {
    return matchers_[0]->matches(cb);
  }

private:
  std::vector<ListenerFilterMatcherPtr> matchers_;
};

ListenerFilterMatcherPtr buildListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config);

} // namespace Server
} // namespace Envoy