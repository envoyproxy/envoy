#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {

class DrainAwareClientConnection;

/**
 * Process-global, thread-local-backed registry of active drain-aware reverse-tunnel client codecs,
 * keyed by cluster name. Codecs register/unregister on their worker thread at construction/
 * destruction; the admin endpoint calls drainCluster() to fan a graceful drain out to every
 * registered codec via runOnAllThreads(), keeping the drain logic in the extension layer.
 */
class UpstreamCodecDrainRegistry : public Singleton::Instance {
public:
  explicit UpstreamCodecDrainRegistry(ThreadLocal::SlotAllocator& tls)
      : tls_(ThreadLocal::TypedSlot<ThreadLocalRegistry>::makeUnique(tls)) {
    tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalRegistry>(); });
  }

  // Called on a worker thread (codec construction). Storing a bare pointer is sufficient; the codec
  // removes itself on destruction.
  void add(absl::string_view cluster, DrainAwareClientConnection& codec) {
    if (auto reg = tls_->get(); reg.has_value()) {
      reg->codecs_[std::string(cluster)].insert(&codec);
    }
  }

  // Called on a worker thread (codec destruction). Drops the now-empty cluster entry so empty sets
  // do not accumulate as dynamic clusters come and go.
  void remove(absl::string_view cluster, DrainAwareClientConnection& codec) {
    if (auto reg = tls_->get(); reg.has_value()) {
      if (auto it = reg->codecs_.find(cluster); it != reg->codecs_.end()) {
        it->second.erase(&codec);
        if (it->second.empty()) {
          reg->codecs_.erase(it);
        }
      }
    }
  }

  // Called on the main thread (admin). Fans a graceful drain out to every registered codec for
  // `cluster` (or all clusters when `cluster` is empty) on each worker thread.
  void drainCluster(absl::string_view cluster, std::chrono::milliseconds drain_time);

private:
  struct ThreadLocalRegistry : public ThreadLocal::ThreadLocalObject {
    absl::flat_hash_map<std::string, absl::flat_hash_set<DrainAwareClientConnection*>> codecs_;
  };

  ThreadLocal::TypedSlotPtr<ThreadLocalRegistry> tls_;
};

using UpstreamCodecDrainRegistrySharedPtr = std::shared_ptr<UpstreamCodecDrainRegistry>;

} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
