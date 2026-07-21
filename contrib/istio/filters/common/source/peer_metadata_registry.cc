#include "contrib/istio/filters/common/source/peer_metadata_registry.h"

#include "envoy/registry/registry.h"
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
  explicit PeerMetadataRegistryImpl(ThreadLocal::SlotAllocator& tls)
      : tls_(ThreadLocal::TypedSlot<ThreadLocalRegistry>::makeUnique(tls)) {
    tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalRegistry>(); });
  }

  void setValue(absl::string_view key, const std::string& value) override {
    if (auto tls = tls_->get(); tls.has_value()) {
      tls->values_[std::string(key)] = value;
    }
  }

  std::optional<std::string> getValue(absl::string_view key) const override {
    if (auto tls = tls_->get(); tls.has_value()) {
      const auto it = tls->values_.find(key);
      if (it != tls->values_.end()) {
        return it->second;
      }
    }
    return std::nullopt;
  }

  void removeValue(absl::string_view key) override {
    if (auto tls = tls_->get(); tls.has_value()) {
      tls->values_.erase(key);
    }
  }

private:
  ThreadLocal::TypedSlotPtr<ThreadLocalRegistry> tls_;
};

} // namespace

SINGLETON_MANAGER_REGISTRATION(peer_metadata_registry);

// Returns the process-wide registry, creating it on first use. The registry is pinned in the
// singleton manager and allocates its Envoy thread-local slot on first creation.
PeerMetadataRegistrySharedPtr getRegistry(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<PeerMetadataRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(peer_metadata_registry),
      [&context] { return std::make_shared<PeerMetadataRegistryImpl>(context.threadLocal()); },
      /*pin=*/true);
}

} // namespace PeerMetadataShared
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
