#pragma once

#include <set>
#include <string>
#include <utility>

#include "envoy/config/watch_map.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Config {

// Because WatchMap implements the SubscriptionCallbacks interface, the xDS client can just
// throw xDS updates directly into WatchMap::onConfigUpdate, which then routes the right
// updates into the right Watches.
class WatchMapImpl : public WatchMap,
                     public SubscriptionCallbacks,
                     public Logger::Loggable<Logger::Id::config> {
public:
  WatchMapImpl() = default;

  WatchPtr addWatch(SubscriptionCallbacks& callbacks) override;
  void removeWatch(Watch* watch) override;
  void setWildcardness(Watch* watch, bool is_wildcard) override;

  std::set<std::string> findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                      Watch* watch) override;
  std::set<std::string> findRemovals(const std::vector<std::string>& newly_removed_from_watch,
                                     Watch* watch) override;

  WatchMapImpl(const WatchMapImpl&) = delete;
  WatchMapImpl& operator=(const WatchMapImpl&) = delete;

private:
  // SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;

  void onConfigUpdateFailed(const EnvoyException* e) override;

  std::string resourceName(const ProtobufWkt::Any&) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  // Returns the union of watch_interest_[resource_name] and wildcard_watches_.
  absl::flat_hash_set<Watch*> watchesInterestedIn(const std::string& resource_name);

  absl::flat_hash_set<Watch*> watches_;

  // Watches whose interest set is currently empty, which is interpreted as "everything".
  absl::flat_hash_set<Watch*> wildcard_watches_;

  // Maps a resource name to the set of watches interested in that resource. Has two purposes:
  // 1) Acts as a reference count; no watches care anymore ==> the resource can be removed.
  // 2) Enables efficient lookup of all interested watches when a resource has been updated.
  absl::flat_hash_map<std::string, absl::flat_hash_set<Watch*>> watch_interest_;
};

} // namespace Config
} // namespace Envoy
