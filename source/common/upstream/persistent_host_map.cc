#include "source/common/upstream/persistent_host_map.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "envoy/upstream/upstream.h"

#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "immer/map.hpp"

namespace Envoy {
namespace Upstream {

// Persistent `immer` CHAMP hash map indexed by host address string. A delta produces a new map that
// structurally shares unchanged nodes with the prior map, so a membership delta is O(delta) instead
// of the O(N) copy the flat HostMap requires.
using PersistentHostMap = immer::map<std::string, HostSharedPtr>;

// The map is published from the main thread and read on workers, which share structural nodes, so
// the node ref-counts must be atomic. Guard against a build that disables `immer` thread safety.
static_assert(
    std::is_same_v<PersistentHostMap::memory_policy_type::refcount, immer::refcount_policy>,
    "PersistentHostMap is shared across threads, so immer must use atomic refcounting. Do not "
    "define IMMER_NO_THREAD_SAFETY.");

namespace {

// Wraps a persistent `immer` map behind the HostLookupTable interface. Used when the cluster opts
// into the persistent backing via `setUsePersistentCrossPriorityHostMap()`.
class PersistentHostLookupTable : public HostLookupTable {
public:
  explicit PersistentHostLookupTable(PersistentHostMap map) : map_(std::move(map)) {}
  HostSharedPtr findHost(absl::string_view address) const override {
    // `immer` keys on `std::string`, so the opt-in persistent path constructs the key. The default
    // flat path stays allocation-free.
    const HostSharedPtr* host = map_.find(std::string(address));
    return host != nullptr ? *host : nullptr;
  }
  size_t size() const override { return map_.size(); }
  bool empty() const override { return map_.empty(); }
  void
  forEach(absl::FunctionRef<void(const std::string&, const HostSharedPtr&)> cb) const override {
    for (const auto& [address, host] : map_) {
      cb(address, host);
    }
  }

private:
  const PersistentHostMap map_;
};

} // namespace

struct PersistentCrossPriorityHostMap::Impl {
  PersistentHostMap map;
};

PersistentCrossPriorityHostMap::PersistentCrossPriorityHostMap()
    : impl_(std::make_unique<Impl>()) {}

PersistentCrossPriorityHostMap::~PersistentCrossPriorityHostMap() = default;

void PersistentCrossPriorityHostMap::seedFrom(const HostMap& flat_map) {
  PersistentHostMap seeded;
  for (const auto& [address, host] : flat_map) {
    seeded = std::move(seeded).set(address, host);
  }
  impl_->map = std::move(seeded);
}

void PersistentCrossPriorityHostMap::exportTo(HostMap& flat_map) const {
  for (const auto& [address, host] : impl_->map) {
    flat_map.insert({address, host});
  }
}

HostSharedPtr PersistentCrossPriorityHostMap::find(const std::string& address) const {
  const HostSharedPtr* host = impl_->map.find(address);
  return host != nullptr ? *host : nullptr;
}

void PersistentCrossPriorityHostMap::set(const std::string& address, const HostSharedPtr& host) {
  impl_->map = std::move(impl_->map).set(address, host);
}

void PersistentCrossPriorityHostMap::erase(const std::string& address) {
  impl_->map = std::move(impl_->map).erase(address);
}

HostLookupTableConstSharedPtr PersistentCrossPriorityHostMap::publish() const {
  return std::make_shared<PersistentHostLookupTable>(impl_->map);
}

} // namespace Upstream
} // namespace Envoy
