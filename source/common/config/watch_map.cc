#include "common/config/watch_map.h"

namespace Envoy {
namespace Config {

Token WatchMap::addWatch(SubscriptionCallbacks& callbacks) {
  Token next_watch = next_watch_++;
  watches_.emplace(next_watch, Watch(callbacks));
}

bool WatchMap::removeWatch(Token token) {
  watches_.erase(token);
  return watches_.empty();
}

std::pair<std::set<std::string>, std::set<std::string>>
WatchMap::updateWatchInterest(Token token, const std::set<std::string>& update_to_these_names) {
  auto watches_entry = watches_.find(token);
  if (watches_entry == watches_.end()) {
    ENVOY_LOG(error, "updateWatchInterest() called on nonexistent token!");
    return;
  }
  auto& watch = watches_entry.second;

  std::vector<std::string> newly_added_to_watch;
  std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                      watch.resource_names_.begin(), watch.resource_names_.end(),
                      std::inserter(newly_added_to_watch, newly_added_to_watch.begin()));

  std::vector<std::string> newly_removed_from_watch;
  std::set_difference(watch.resource_names_.begin(), watch.resource_names_.end(),
                      update_to_these_names.begin(), update_to_these_names.end(),
                      std::inserter(newly_removed_from_watch, newly_removed_from_watch.begin()));

  watch.resource_names_ = update_to_these_names;

  return std::make_pair(findAdditions(newly_added_to_watch, token),
                        findRemovals(newly_removed_from_watch, token));
}

const absl::flat_hash_set<Token>& WatchMap::tokensInterestedIn(const std::string& resource_name) {
  auto entry = watch_interest_.find(resource_name);
  if (entry == watch_interest_.end()) {
    return {};
  }
  return entry.second;
}

void WatchMap::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    ENVOY_LOG(warn, "WatchMap::onConfigUpdate: there are no watches!");
    return;
  }
  SubscriptionCallbacks& name_getter = watches_.front()->callbacks_;

  // Build a map from watches, to the set of updated resources that each watch cares about. Each
  // entry in the map is then a nice little bundle that can be fed directly into the individual
  // onConfigUpdate()s.
  absl::flat_hash_map<Token, Protobuf::RepeatedPtrField<ProtobufWkt::Any>> per_watch_updates;
  for (const auto& r : resources) {
    const absl::flat_hash_set<Token>& interested_in_r =
        tokensInterestedIn(name_getter.resourceName(r));
    for (interested_token : interested_in_r) {
      per_watch_updates[interested_token].Add()->CopyFrom(r);
    }
  }

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& updated : per_watch_updates) {
    auto entry = watches_.find(updated.first);
    if (entry == watches_.end()) {
      ENVOY_LOG(error, "A token referred to by watch_interest_ is not present in watches_!");
      continue;
    }
    entry.second.callbacks_.onConfigUpdate(updated.second, version_info);
  }
}

void WatchMap::tryDeliverConfigUpdate(
    Token token, const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  auto entry = watches_.find(token);
  if (entry == watches_.end()) {
    ENVOY_LOG(error, "A token referred to by watch_interest_ is not present in watches_!");
    return;
  }
  entry.second.callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
}

void WatchMap::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  if (watches_.empty()) {
    ENVOY_LOG(warn, "WatchMap::onConfigUpdate: there are no watches!");
    return;
  }
  // Build a pair of maps: from watches, to the set of resources {added,removed} that each watch
  // cares about. Each entry in the map-pair is then a nice little bundle that can be fed directly
  // into the individual onConfigUpdate()s.
  absl::flat_hash_map<Token, Protobuf::RepeatedPtrField<envoy::api::v2::Resource>> per_watch_added;
  absl::flat_hash_map<Token, Protobuf::RepeatedPtrField<std::string>> per_watch_removed;
  for (const auto& r : added_resources) {
    const absl::flat_hash_set<Token>& interested_in_r = tokensInterestedIn(r.name());
    for (interested_token : interested_in_r) {
      per_watch_added[interested_token].Add()->CopyFrom(r);
    }
  }
  for (const auto& r : removed_resources) {
    const absl::flat_hash_set<Token>& tokens_interested_in_r = tokensInterestedIn(r);
    for (interested_token : tokens_interested_in_r.second) {
      *per_watch_removed[interested_token].Add() = r;
    }
  }

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& added : per_watch_added) {
    const Token& cur_token = added.first;
    auto removed = per_watch_removed.find(cur_token);
    if (removed == per_watch_removed.end()) {
      // additions only, no removals
      tryDeliverConfigUpdate(cur_token, added.second, {}, system_version_info);
    } else {
      // both additions and removals
      tryDeliverConfigUpdate(cur_token, added.second, removed.second, system_version_info);
      // Drop the removals now, so the final removals-only pass won't use them.
      per_watch_removed.erase(removed);
    }
  }
  // Any removals-only updates will not have been picked up in the per_watch_added loop.
  for (const auto& removed : per_watch_removed) {
    tryDeliverConfigUpdate(removed.first, {}, removed.second, system_version_info);
  }
}

void WatchMap::onConfigUpdateFailed(const EnvoyException* e) {
  for (auto& watch : watches_) {
    watch.second.onConfigUpdateFailed(e);
  }
}

std::set<std::string> WatchMap::findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                              Token token) {
  std::set<std::string> newly_added_to_subscription;
  for (const auto& name : newly_added_to_watch) {
    auto entry = watch_interest_.find(name);
    if (entry == watch_interest_.end()) {
      newly_added_to_subscription.insert(name);
      watch_interest_.emplace(name, {token})
    } else {
      entry.second.insert(token);
    }
  }
  return newly_added_to_subscription;
}

std::set<std::string>
WatchMap::findRemovals(const std::vector<std::string>& newly_removed_from_watch, Token token) {
  std::set<std::string> newly_removed_from_subscription;
  for (const auto& name : newly_removed_from_watch) {
    auto entry = watch_interest_.find(name);
    if (entry == watch_interest_.end()) {
      ENVOY_LOG(warn, "WatchMap: tried to remove a watch from untracked resource {}", name);
      continue;
    }
    entry.second.erase(token);
    if (entry.second.empty()) {
      watch_interest_.erase(entry);
    }
    newly_removed_from_subscription.insert(name);
  }
  return newly_removed_from_subscription;
}

} // namespace Config
} // namespace Envoy
