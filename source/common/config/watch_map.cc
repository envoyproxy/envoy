#include "common/config/watch_map.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/cleanup.h"
#include "common/config/decoded_resource_impl.h"

namespace Envoy {
namespace Config {

Watch* WatchMap::addWatch(SubscriptionCallbacks& callbacks,
                          OpaqueResourceDecoder& resource_decoder) {
  auto watch = std::make_unique<Watch>(callbacks, resource_decoder);
  Watch* watch_ptr = watch.get();
  wildcard_watches_.insert(watch_ptr);
  watches_.insert(std::move(watch));
  return watch_ptr;
}

void WatchMap::removeWatch(Watch* watch) {
  if (deferred_removed_during_update_ != nullptr) {
    deferred_removed_during_update_->insert(watch);
  } else {
    wildcard_watches_.erase(watch); // may or may not be in there, but we want it gone.
    watches_.erase(watch);
  }
}

void WatchMap::removeDeferredWatches() {
  for (auto& watch : *deferred_removed_during_update_) {
    wildcard_watches_.erase(watch); // may or may not be in there, but we want it gone.
    watches_.erase(watch);
  }
  deferred_removed_during_update_ = nullptr;
}

AddedRemoved WatchMap::updateWatchInterest(Watch* watch,
                                           const std::set<std::string>& update_to_these_names) {
  if (update_to_these_names.empty()) {
    wildcard_watches_.insert(watch);
  } else {
    wildcard_watches_.erase(watch);
  }

  std::vector<std::string> newly_added_to_watch;
  std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                      watch->resource_names_.begin(), watch->resource_names_.end(),
                      std::inserter(newly_added_to_watch, newly_added_to_watch.begin()));

  std::vector<std::string> newly_removed_from_watch;
  std::set_difference(watch->resource_names_.begin(), watch->resource_names_.end(),
                      update_to_these_names.begin(), update_to_these_names.end(),
                      std::inserter(newly_removed_from_watch, newly_removed_from_watch.begin()));

  watch->resource_names_ = update_to_these_names;

  return AddedRemoved(findAdditions(newly_added_to_watch, watch),
                      findRemovals(newly_removed_from_watch, watch));
}

absl::flat_hash_set<Watch*> WatchMap::watchesInterestedIn(const std::string& resource_name) {
  absl::flat_hash_set<Watch*> ret = wildcard_watches_;
  const auto watches_interested = watch_interest_.find(resource_name);
  if (watches_interested != watch_interest_.end()) {
    for (const auto& watch : watches_interested->second) {
      ret.insert(watch);
    }
  }
  return ret;
}

void WatchMap::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    return;
  }

  // Track any removals triggered by earlier watch updates.
  ASSERT(deferred_removed_during_update_ == nullptr);
  deferred_removed_during_update_ = std::make_unique<absl::flat_hash_set<Watch*>>();
  Cleanup cleanup([this] { removeDeferredWatches(); });
  // Build a map from watches, to the set of updated resources that each watch cares about. Each
  // entry in the map is then a nice little bundle that can be fed directly into the individual
  // onConfigUpdate()s.
  std::vector<DecodedResourceImplPtr> decoded_resources;
  absl::flat_hash_map<Watch*, std::vector<DecodedResourceRef>> per_watch_updates;
  for (const auto& r : resources) {
    decoded_resources.emplace_back(
        new DecodedResourceImpl((*watches_.begin())->resource_decoder_, r, version_info));
    const absl::flat_hash_set<Watch*>& interested_in_r =
        watchesInterestedIn(decoded_resources.back()->name());
    for (const auto& interested_watch : interested_in_r) {
      per_watch_updates[interested_watch].emplace_back(*decoded_resources.back());
    }
  }

  const bool map_is_single_wildcard = (watches_.size() == 1 && wildcard_watches_.size() == 1);
  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (auto& watch : watches_) {
    if (deferred_removed_during_update_->count(watch.get()) > 0) {
      continue;
    }
    const auto this_watch_updates = per_watch_updates.find(watch);
    if (this_watch_updates == per_watch_updates.end()) {
      // This update included no resources this watch cares about.
      // 1) If there is only a single, wildcard watch (i.e. Cluster or Listener), always call
      //    its onConfigUpdate even if just a no-op, to properly maintain state-of-the-world
      //    semantics and the update_empty stat.
      // 2) If this watch previously had some resources, it means this update is removing all
      //    of this watch's resources, so the watch must be informed with an onConfigUpdate.
      // 3) Otherwise, we can skip onConfigUpdate for this watch.
      if (map_is_single_wildcard || !watch->state_of_the_world_empty_) {
        watch->state_of_the_world_empty_ = true;
        watch->callbacks_.onConfigUpdate({}, version_info);
      }
    } else {
      watch->state_of_the_world_empty_ = false;
      watch->callbacks_.onConfigUpdate(this_watch_updates->second, version_info);
    }
  }
}

// For responses to on-demand requests, replace the original watch for an alias
// with one for the resource's name
AddedRemoved WatchMap::convertAliasWatchesToNameWatches(
    const envoy::service::discovery::v3::Resource& resource) {
  absl::flat_hash_set<Watch*> watches_to_update;
  for (const auto& alias : resource.aliases()) {
    const auto interested_watches = watch_interest_.find(alias);
    if (interested_watches != watch_interest_.end()) {
      for (const auto& interested_watch : interested_watches->second) {
        watches_to_update.insert(interested_watch);
      }
    }
  }

  auto ret = AddedRemoved({}, {});
  for (const auto& watch : watches_to_update) {
    const auto& converted_watches = updateWatchInterest(watch, {resource.name()});
    std::copy(converted_watches.added_.begin(), converted_watches.added_.end(),
              std::inserter(ret.added_, ret.added_.end()));
    std::copy(converted_watches.removed_.begin(), converted_watches.removed_.end(),
              std::inserter(ret.removed_, ret.removed_.end()));
  }

  return ret;
}

void WatchMap::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  // Track any removals triggered by earlier watch updates.
  ASSERT(deferred_removed_during_update_ == nullptr);
  deferred_removed_during_update_ = std::make_unique<absl::flat_hash_set<Watch*>>();
  Cleanup cleanup([this] { removeDeferredWatches(); });
  // Build a pair of maps: from watches, to the set of resources {added,removed} that each watch
  // cares about. Each entry in the map-pair is then a nice little bundle that can be fed directly
  // into the individual onConfigUpdate()s.
  std::vector<DecodedResourceImplPtr> decoded_resources;
  absl::flat_hash_map<Watch*, std::vector<DecodedResourceRef>> per_watch_added;
  for (const auto& r : added_resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r = watchesInterestedIn(r.name());
    // If there are no watches, then we don't need to decode. If there are watches, they should all
    // be for the same resource type, so we can just use the callbacks of the first watch to decode.
    if (interested_in_r.empty()) {
      continue;
    }
    decoded_resources.emplace_back(
        new DecodedResourceImpl((*interested_in_r.begin())->resource_decoder_, r));
    for (const auto& interested_watch : interested_in_r) {
      per_watch_added[interested_watch].emplace_back(*decoded_resources.back());
    }
  }
  absl::flat_hash_map<Watch*, Protobuf::RepeatedPtrField<std::string>> per_watch_removed;
  for (const auto& r : removed_resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r = watchesInterestedIn(r);
    for (const auto& interested_watch : interested_in_r) {
      *per_watch_removed[interested_watch].Add() = r;
    }
  }

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& [cur_watch, resource] : per_watch_added) {
    if (deferred_removed_during_update_->count(cur_watch) > 0) {
      continue;
    }
    const auto removed = per_watch_removed.find(cur_watch);
    if (removed == per_watch_removed.end()) {
      // additions only, no removals
      cur_watch->callbacks_.onConfigUpdate(resource, {}, system_version_info);
    } else {
      // both additions and removals
      cur_watch->callbacks_.onConfigUpdate(resource, removed->second, system_version_info);
      // Drop the removals now, so the final removals-only pass won't use them.
      per_watch_removed.erase(removed);
    }
  }
  // Any removals-only updates will not have been picked up in the per_watch_added loop.
  for (auto& [cur_watch, resource] : per_watch_removed) {
    if (deferred_removed_during_update_->count(cur_watch) > 0) {
      continue;
    }
    cur_watch->callbacks_.onConfigUpdate({}, resource, system_version_info);
  }
}

void WatchMap::onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) {
  for (auto& watch : watches_) {
    watch->callbacks_.onConfigUpdateFailed(reason, e);
  }
}

std::set<std::string> WatchMap::findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                              Watch* watch) {
  std::set<std::string> newly_added_to_subscription;
  for (const auto& name : newly_added_to_watch) {
    auto entry = watch_interest_.find(name);
    if (entry == watch_interest_.end()) {
      newly_added_to_subscription.insert(name);
      watch_interest_[name] = {watch};
    } else {
      // Add this watch to the already-existing set at watch_interest_[name]
      entry->second.insert(watch);
    }
  }
  return newly_added_to_subscription;
}

std::set<std::string>
WatchMap::findRemovals(const std::vector<std::string>& newly_removed_from_watch, Watch* watch) {
  std::set<std::string> newly_removed_from_subscription;
  for (const auto& name : newly_removed_from_watch) {
    auto entry = watch_interest_.find(name);
    RELEASE_ASSERT(
        entry != watch_interest_.end(),
        fmt::format("WatchMap: tried to remove a watch from untracked resource {}", name));

    entry->second.erase(watch);
    if (entry->second.empty()) {
      watch_interest_.erase(entry);
      newly_removed_from_subscription.insert(name);
    }
  }
  return newly_removed_from_subscription;
}

} // namespace Config
} // namespace Envoy
