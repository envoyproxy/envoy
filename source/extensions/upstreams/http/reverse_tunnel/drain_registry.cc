#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"

#include <vector>

#include "source/extensions/upstreams/http/reverse_tunnel/drain_aware_client_connection.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

void UpstreamCodecDrainRegistry::drainCluster(absl::string_view cluster,
                                              std::chrono::milliseconds drain_time) {
  const std::string key{cluster};
  tls_->runOnAllThreads([key, drain_time](OptRef<ThreadLocalRegistry> reg) {
    if (!reg.has_value()) {
      return;
    }
    // Draining a codec can synchronously close its connection, which destroys the codec and
    // unregisters it -- mutating both the per-cluster set and the codecs_ map (an emptied entry is
    // erased). To stay safe against that reentrant removal we (1) copy the target cluster keys
    // before iterating, (2) re-find the entry and (3) confirm the codec is still registered before
    // each drain, so we never iterate a mutated container or dereference a stale pointer.
    std::vector<std::string> cluster_keys;
    if (key.empty()) {
      cluster_keys.reserve(reg->codecs_.size());
      for (const auto& entry : reg->codecs_) {
        cluster_keys.push_back(entry.first);
      }
    } else {
      cluster_keys.push_back(key);
    }
    for (const auto& cluster_key : cluster_keys) {
      auto it = reg->codecs_.find(cluster_key);
      if (it == reg->codecs_.end()) {
        continue;
      }
      const std::vector<DrainAwareClientConnection*> snapshot(it->second.begin(), it->second.end());
      for (auto* codec : snapshot) {
        auto live = reg->codecs_.find(cluster_key);
        if (live == reg->codecs_.end() || !live->second.contains(codec)) {
          continue;
        }
        codec->startGracefulDrain(drain_time);
      }
    }
  });
}

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
