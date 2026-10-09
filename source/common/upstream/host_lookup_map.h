#pragma once

#include <functional>
#include <string>
#include <utility>

#include "envoy/upstream/upstream.h"

#include "source/common/common/macros.h"
#include "source/common/common/persistent_hash_map.h"

#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

/**
 * @return const HostSharedPtr& the null host returned by lookups of an absent address.
 */
inline const HostSharedPtr& nullHost() { CONSTRUCT_ON_FIRST_USE(HostSharedPtr); }

/**
 * HostLookupMap backed by a flat HostMap. This is the default backing of the cross priority host
 * map.
 */
class FlatHostLookupMap : public HostLookupMap {
public:
  explicit FlatHostLookupMap(HostMapConstSharedPtr map) : map_(std::move(map)) {}

  // Upstream::HostLookupMap
  const HostSharedPtr& findHost(absl::string_view address) const override {
    const auto it = map_->find(address);
    return it != map_->end() ? it->second : nullHost();
  }
  size_t size() const override { return map_->size(); }
  bool empty() const override { return map_->empty(); }
  void forEach(absl::FunctionRef<void(absl::string_view, const HostSharedPtr&)> cb) const override {
    for (const auto& [address, host] : *map_) {
      cb(address, host);
    }
  }

private:
  const HostMapConstSharedPtr map_;
};

/**
 * Persistent host map indexed by host address string. Copying it is an O(1) snapshot and every
 * update is O(log N). See PersistentHashMap.
 */
using PersistentHostMap =
    PersistentHashMap<std::string, HostSharedPtr, absl::Hash<absl::string_view>, std::equal_to<>>;

/**
 * HostLookupMap backed by a PersistentHostMap snapshot.
 */
class PersistentHostLookupMap : public HostLookupMap {
public:
  explicit PersistentHostLookupMap(PersistentHostMap map) : map_(std::move(map)) {}

  // Upstream::HostLookupMap
  const HostSharedPtr& findHost(absl::string_view address) const override {
    const HostSharedPtr* host = map_.find(address);
    return host != nullptr ? *host : nullHost();
  }
  size_t size() const override { return map_.size(); }
  bool empty() const override { return map_.empty(); }
  void forEach(absl::FunctionRef<void(absl::string_view, const HostSharedPtr&)> cb) const override {
    map_.forEach(cb);
  }

private:
  const PersistentHostMap map_;
};

} // namespace Upstream
} // namespace Envoy
