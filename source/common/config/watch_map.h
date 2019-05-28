#pragma once

namespace Envoy {
namespace Config {

// An opaque token given out to users of WatchMap, to identify a given watch.
using WatchToken = uint64_t;

// Manages "watches" of xDS resources. Several xDS callers might ask for a subscription to the same
// resource name "X". The xDS machinery must return to each their very own subscription to X.
// The xDS machinery's "watch" concept accomplishes that, while avoiding parallel reduntant xDS
// requests for X. Each of those subscriptions is viewed as a "watch" on X, while behind the scenes
// there is just a single real subscription to that resource name.
// This class maintains the mapping between those two: it
// 1) delivers updates to all interested watches, and
// 2) adds/removes resource names to/from the subscription when first/last watch is added/removed.
//
// #1 is accomplished by WatchMap's implementation of the SubscriptionCallbacks interface.
// This interface allows the xDS client to just throw each xDS update message it receives directly
// into WatchMap::onConfigUpdate, rather than having to track the various watches' callbacks.
//
// A WatchMap is assumed to be dedicated to a single type_url type of resource (EDS, CDS, etc).
class WatchMap : public SubscriptionCallbacks {
public:
  WatchToken addWatch(SubscriptionCallbacks& callbacks) override {
    WatchToken next_watch = next_watch_++;
    watches_[next_watch] = Watch(callbacks);
  }

  // Returns true if this was the very last watch in the map.
  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  bool removeWatch(WatchToken watch_token) {
    watches_.erase(watch_token);
    return watches_.empty();
  }

  // Returns resource names added and removed across all watches. That is:
  // 1) if 'resources' contains X and no other watch cares about X, X will be in pair.first.
  // 2) if 'resources' does not contain Y, and this watch was the only one that cared about Y,
  //    Y will be in pair.second.
  std::pair<std::set<std::string>, std::set<std::string>>
  updateWatchInterest(WatchToken watch_token, const std::set<std::string>& update_to_these_names) {
    std::vector<std::string> newly_added_to_watch;
    std::vector<std::string> newly_removed_from_watch;
    std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                        watches_[watch_token].resource_names_.begin(),
                        watches_[watch_token].resource_names_.end(),
                        std::inserter(newly_added_to_watch, newly_added_to_watch.begin()));
    std::set_difference(watches_[watch_token].resource_names_.begin(),
                        watches_[watch_token].resource_names_.end(), update_to_these_names.begin(),
                        update_to_these_names.end(),
                        std::inserter(newly_removed_from_watch, newly_removed_from_watch.begin()));

    watches_[watch_token].resource_names_ = update_to_these_names;

    return std::make_pair(findAdditions(newly_added_to_watch, watch_token),
                          findRemovals(newly_removed_from_watch, watch_token));
  }

  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) override {
    if (watches_.empty()) {
      ENVOY_LOG(warn, "WatchMap::onConfigUpdate: there are no watches!");
      return;
    }
    absl::flat_hash_map<WatchToken, Protobuf::RepeatedPtrField<ProtobufWkt::Any>> per_watch_updates;
    GrpcMuxCallbacks& name_getter = watches_.front()->callbacks_;
    for (const auto& r : resources) {
      const absl::flat_hash_set<WatchToken>& interested_in_r =
          watch_interest_.find(name_getter.resourceName(r));
      for (interested_token : interested_in_r)
        per_watch_added[interested_token].Add()->CopyFrom(r);
    }

    // We just bundled up the updates into nice per-watch packages. Now, deliver them.
    for (const auto& updated : per_watch_updates) {
      watches_[updated.first].callbacks_.onConfigUpdate(updated.second, version_info);
    }
  }

  virtual void
  onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) override {
    absl::flat_hash_map<WatchToken, Protobuf::RepeatedPtrField<envoy::api::v2::Resource>>
        per_watch_added;
    absl::flat_hash_map<WatchToken, Protobuf::RepeatedPtrField<std::string>> per_watch_removed;
    for (const auto& r : added_resources) {
      const absl::flat_hash_set<WatchToken>& interested_in_r = watch_interest_.find(r.name());
      for (interested_token : interested_in_r)
        per_watch_added[interested_token].Add()->CopyFrom(r);
    }
    for (const auto& r : removed_resources) {
      const absl::flat_hash_set<WatchToken>& interested_in_r = watch_interest_.find(r);
      for (interested_token : interested_in_r)
        *per_watch_removed[interested_token].Add() = r;
    }

    // We just bundled up the updates into nice per-watch packages. Now, deliver them.
    for (const auto& added : per_watch_added) {
      auto removed = per_watch_removed.find(added.first);
      if (removed == per_watch_removed.end()) { // additions only, no removals
        watches_[added.first].callbacks_.onConfigUpdate(per_watch_added.second, {},
                                                        system_version_info);
      } else { // both additions and removals
        watches_[added.first].callbacks_.onConfigUpdate(per_watch_added.second, removed.second,
                                                        system_version_info);
        // Drop it now, so the final removals-only pass won't accidentally use it.
        per_watch_removed.erase(removed);
      }
    }
    // Any removals-only updates will not have been picked up in the per_watch_added loop.
    for (const auto& removed : per_watch_removed) {
      watches_[removed.first].callbacks_.onConfigUpdate({}, removed.second, system_version_info);
    }
  }

  virtual void onConfigUpdateFailed(const EnvoyException* e) override {
    for (auto& watch : watches_) {
      watch.second.onConfigUpdateFailed(e);
    }
  }

  virtual std::string resourceName(const ProtobufWkt::Any& resource) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  struct Watch {
    std::set<std::string> resource_names_; // must be sorted set, for set_difference.
    GrpcMuxCallbacks& callbacks_;
  };

  // Given a list of names that are new to an individual watch, returns those names that are in fact
  // new to the entire subscription.
  std::set<std::string> findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                      WatchToken watch_token) {
    std::set<std::string> newly_added_to_subscription;
    for (const auto& name : newly_added_to_watch) {
      auto entry = watch_interest_.find(name);
      if (entry == watch_interest_.end()) {
        newly_added_to_subscription.insert(name);
        watch_interest_.emplace(name, {watch_token})
      } else {
        entry.second.insert(watch_token);
      }
    }
    return newly_added_to_subscription;
  }

  // Given a list of names that an individual watch no longer cares about, returns those names that
  // in fact the entire subscription no longer cares about.
  std::set<std::string> findRemovals(const std::vector<std::string>& newly_removed_from_watch,
                                     WatchToken watch_token) {
    std::set<std::string> newly_removed_from_subscription;
    for (const auto& name : newly_removed_from_watch) {
      auto entry = watch_interest_.find(name);
      if (entry == watch_interest_.end()) {
        ENVOY_LOG(warn, "WatchMap: tried to remove a watch from untracked resource {}", name);
        continue;
      }
      entry.second.erase(watch_token);
      if (entry.second.empty()) {
        watch_interest_.erase(entry);
      }
      newly_removed_from_subscription.insert(name);
    }
    return newly_removed_from_subscription;
  }

  absl::flat_hash_map<WatchToken, Watch> watches_;
  // Maps a resource name to the set of watches interested in that resource. Has two purposes:
  // 1) Acts as a reference count; no watches care anymore ==> the resource can be removed.
  // 2) Enables efficient lookup of all interested watches when a resource has been updated.
  absl::flat_hash_map<std::string, absl::flat_hash_set<WatchToken>> watch_interest_;

  WatchToken next_watch_{0};

  WatchMap(const WatchMap&) = delete;
  WatchMap& operator=(const WatchMap&) = delete;
};

} // namespace Config
} // namespace Envoy
