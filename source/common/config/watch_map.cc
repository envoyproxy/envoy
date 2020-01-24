#include "common/config/watch_map.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

namespace Envoy {
namespace Config {

Watch* WatchMap::addWatch(SubscriptionCallbacks& callbacks) {
  auto watch = std::make_unique<Watch>(callbacks);
  Watch* watch_ptr = watch.get();
  wildcard_watches_.insert(watch_ptr);
  watches_.insert(std::move(watch));
  return watch_ptr;
}

void WatchMap::removeWatch(Watch* watch) {
  wildcard_watches_.erase(watch); // may or may not be in there, but we want it gone.
  watches_.erase(watch);
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
  SubscriptionCallbacks& name_getter = (*watches_.begin())->callbacks_;

  // Build a map from watches, to the set of updated resources that each watch cares about. Each
  // entry in the map is then a nice little bundle that can be fed directly into the individual
  // onConfigUpdate()s.
  absl::flat_hash_map<Watch*, Protobuf::RepeatedPtrField<ProtobufWkt::Any>> per_watch_updates;
  for (const auto& r : resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r =
        watchesInterestedIn(name_getter.resourceName(r));
    for (const auto& interested_watch : interested_in_r) {
      per_watch_updates[interested_watch].Add()->CopyFrom(r);
    }
  }

  const bool map_is_single_wildcard = (watches_.size() == 1 && wildcard_watches_.size() == 1);
  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (auto& watch : watches_) {
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
        watch->callbacks_.onConfigUpdate({}, version_info);
        watch->state_of_the_world_empty_ = true;
      }
    } else {
      watch->callbacks_.onConfigUpdate(this_watch_updates->second, version_info);
      watch->state_of_the_world_empty_ = false;
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
  // Build a pair of maps: from watches, to the set of resources {added,removed} that each watch
  // cares about. Each entry in the map-pair is then a nice little bundle that can be fed directly
  // into the individual onConfigUpdate()s.
  absl::flat_hash_map<Watch*, Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>>
      per_watch_added;
  for (const auto& r : added_resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r = watchesInterestedIn(r.name());
    for (const auto& interested_watch : interested_in_r) {
      per_watch_added[interested_watch].Add()->CopyFrom(r);
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
  for (const auto& added : per_watch_added) {
    const Watch* cur_watch = added.first;
    const auto removed = per_watch_removed.find(cur_watch);
    if (removed == per_watch_removed.end()) {
      // additions only, no removals
      cur_watch->callbacks_.onConfigUpdate(added.second, {}, system_version_info);
    } else {
      // both additions and removals
      cur_watch->callbacks_.onConfigUpdate(added.second, removed->second, system_version_info);
      // Drop the removals now, so the final removals-only pass won't use them.
      per_watch_removed.erase(removed);
    }
  }
  // Any removals-only updates will not have been picked up in the per_watch_added loop.
  for (auto& removed : per_watch_removed) {
    removed.first->callbacks_.onConfigUpdate({}, removed.second, system_version_info);
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
