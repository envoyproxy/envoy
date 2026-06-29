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
    const auto drain_set = [drain_time](absl::flat_hash_set<DrainAwareClientConnection*>& codecs) {
      // Snapshot first: startGracefulDrain() can indirectly mutate the set via timer/close paths.
      std::vector<DrainAwareClientConnection*> snapshot(codecs.begin(), codecs.end());
      for (auto* codec : snapshot) {
        codec->startGracefulDrain(drain_time);
      }
    };
    if (key.empty()) {
      for (auto& entry : reg->codecs_) {
        drain_set(entry.second);
      }
    } else if (auto it = reg->codecs_.find(key); it != reg->codecs_.end()) {
      drain_set(it->second);
    }
  });
}

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
