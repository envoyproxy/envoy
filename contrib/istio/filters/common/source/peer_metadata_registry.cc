#include "contrib/istio/filters/common/source/peer_metadata_registry.h"

#include "envoy/registry/registry.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace PeerMetadataShared {

namespace {

struct ThreadLocalRegistry : public ThreadLocal::ThreadLocalObject {
  absl::flat_hash_map<std::string, std::string> values_;
};

class PeerMetadataRegistryImpl : public PeerMetadataRegistry, public Singleton::Instance {
public:
  // When use_thread_local_slot is true (the production default) the registry is backed by an
  // Envoy ThreadLocal::TypedSlot. When false, it uses a process-local thread_local fallback and
  // allocates no Envoy TLS slot. The fallback exists so integration tests that run multiple Envoy
  // servers in one process can avoid perturbing the shared, index-based internal-listener TLS slot
  // layout (see the istio.peer_metadata.use_thread_local_slot runtime key in getRegistry()).
  PeerMetadataRegistryImpl(ThreadLocal::SlotAllocator& tls, bool use_thread_local_slot) {
    if (use_thread_local_slot) {
      tls_ = ThreadLocal::TypedSlot<ThreadLocalRegistry>::makeUnique(tls);
      tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalRegistry>(); });
    }
  }

  void setValue(absl::string_view key, const std::string& value) override {
    if (tls_ != nullptr) {
      if (auto tls = tls_->get(); tls.has_value()) {
        tls->values_[std::string(key)] = value;
      }
      return;
    }
    fallbackMap()[std::string(key)] = value;
  }

  std::optional<std::string> getValue(absl::string_view key) const override {
    if (tls_ != nullptr) {
      if (auto tls = tls_->get(); tls.has_value()) {
        const auto it = tls->values_.find(key);
        if (it != tls->values_.end()) {
          return it->second;
        }
      }
      return std::nullopt;
    }
    const auto& map = fallbackMap();
    const auto it = map.find(key);
    if (it != map.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void removeValue(absl::string_view key) override {
    if (tls_ != nullptr) {
      if (auto tls = tls_->get(); tls.has_value()) {
        tls->values_.erase(key);
      }
      return;
    }
    fallbackMap().erase(key);
  }

private:
  // Process-local, per-worker-thread fallback used when the Envoy TLS slot is disabled.
  static absl::flat_hash_map<std::string, std::string>& fallbackMap() {
    static thread_local absl::flat_hash_map<std::string, std::string> map;
    return map;
  }

  // Null when the TLS slot is disabled; fallbackMap() is used instead.
  ThreadLocal::TypedSlotPtr<ThreadLocalRegistry> tls_;
};

} // namespace

SINGLETON_MANAGER_REGISTRATION(peer_metadata_registry);

PeerMetadataRegistrySharedPtr getRegistry(Server::Configuration::ServerFactoryContext& context) {
  // Defaults to true (production behavior: back the registry with an Envoy TLS slot).
  // Integration tests that run multiple Envoy servers in one process set this to false to avoid
  // allocating an Envoy TLS slot, which would otherwise perturb the shared, index-based
  // internal-listener TLS slot layout and abort the other in-process server on connection creation.
  const bool use_thread_local_slot =
      context.runtime().snapshot().getBoolean("istio.peer_metadata.use_thread_local_slot", true);
  return context.singletonManager().getTyped<PeerMetadataRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(peer_metadata_registry),
      [&context, use_thread_local_slot] {
        return std::make_shared<PeerMetadataRegistryImpl>(context.threadLocal(),
                                                          use_thread_local_slot);
      },
      /*pin=*/true);
}

} // namespace PeerMetadataShared
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
