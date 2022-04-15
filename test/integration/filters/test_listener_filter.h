#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
/**
 * Test listener filter which sets the ALPN to a manually configured string.
 */
class TestListenerFilter : public Network::ListenerFilter {
public:
  TestListenerFilter() = default;

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    absl::MutexLock m(&alpn_lock_);
    ASSERT(!alpn_.empty());
    cb.socket().setRequestedApplicationProtocols({alpn_});
    alpn_.clear();
    return Network::FilterStatus::Continue;
  }

  static void setAlpn(std::string alpn) {
    absl::MutexLock m(&alpn_lock_);
    alpn_ = alpn;
  }

private:
  static absl::Mutex alpn_lock_;
  static std::string alpn_;
};

} // namespace Envoy
