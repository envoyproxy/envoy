#include "source/extensions/config_subscription/grpc/watch_map.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/utility.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"

namespace Envoy {
namespace Config {

namespace {
// Returns the namespace part (if there's any) in the resource name.
std::string namespaceFromName(const std::string& resource_name) {
  // We simply remove the last / component. E.g. www.foo.com/bar becomes www.foo.com.
  const auto pos = resource_name.find_last_of('/');
  // We are not interested in the "/" character in the namespace
  return pos == std::string::npos ? "" : resource_name.substr(0, pos);
}
} // namespace

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

AddedRemoved
WatchMap::updateWatchInterest(Watch* watch,
                              const absl::flat_hash_set<std::string>& update_to_these_names) {
  if (update_to_these_names.empty() || update_to_these_names.contains(Wildcard)) {
    wildcard_watches_.insert(watch);
  } else {
    wildcard_watches_.erase(watch);
  }

  absl::flat_hash_set<std::string> newly_added_to_watch;
  SetUtil::setDifference(update_to_these_names, watch->resource_names_, newly_added_to_watch);

  absl::flat_hash_set<std::string> newly_removed_from_watch;
  SetUtil::setDifference(watch->resource_names_, update_to_these_names, newly_removed_from_watch);

  watch->resource_names_ = update_to_these_names;

  // First resources are added and only then removed, so a watch won't be removed
  // if its interest has been replaced (rather than completely removed).
  absl::flat_hash_set<std::string> added_resources = findAdditions(newly_added_to_watch, watch);
  absl::flat_hash_set<std::string> removed_resources =
      findRemovals(newly_removed_from_watch, watch);
  // Remove cached resource that are no longer relevant.
  if (eds_resources_cache_.has_value()) {
    for (const auto& resource_name : removed_resources) {
      // This may pass a resource_name that is not in the cache, for example
      // if the resource contents has never arrived.
      eds_resources_cache_->removeResource(resource_name);
    }
  }
  return {std::move(added_resources), std::move(removed_resources)};
}

absl::flat_hash_set<Watch*> WatchMap::watchesInterestedIn(const std::string& resource_name) {
  absl::flat_hash_set<Watch*> ret;
  if (!use_namespace_matching_) {
    ret = wildcard_watches_;
  }
  const bool is_xdstp = XdsResourceIdentifier::hasXdsTpScheme(resource_name);
  xds::core::v3::ResourceName xdstp_resource;
  XdsResourceIdentifier::EncodeOptions encode_options;
  encode_options.sort_context_params_ = true;
  // First look for an exact match. If this is xdstp:// we need to normalize context parameters.
  if (is_xdstp) {
    // TODO(htuch): factor this (and stuff in namespaceFromName) into a dedicated library.
    // This is not very efficient; it is possible to canonicalize etc. much faster with raw string
    // operations, but this implementation provides a reference for later optimization while we
    // adopt xdstp://.
    auto resource_or_error = XdsResourceIdentifier::decodeUrn(resource_name);
    THROW_IF_STATUS_NOT_OK(resource_or_error, throw);
    xdstp_resource = resource_or_error.value();
  }
  auto watches_interested = watch_interest_.find(
      is_xdstp ? XdsResourceIdentifier::encodeUrn(xdstp_resource, encode_options) : resource_name);
  // If that fails, consider namespace/glob matching. This is the slow path for xdstp:// and should
  // only happen for glob collections. TODO(htuch): It should be possible to have much more
  // efficient matchers here.
  if (watches_interested == watch_interest_.end()) {
    if (use_namespace_matching_) {
      watches_interested = watch_interest_.find(namespaceFromName(resource_name));
    } else if (is_xdstp) {
      // Replace resource name component with glob for purpose of matching.
      const auto pos = xdstp_resource.id().find_last_of('/');
      xdstp_resource.set_id(pos == std::string::npos ? "*"
                                                     : xdstp_resource.id().substr(0, pos) + "/*");
      const std::string encoded_name =
          XdsResourceIdentifier::encodeUrn(xdstp_resource, encode_options);
      watches_interested = watch_interest_.find(encoded_name);
    }
  }
  if (watches_interested != watch_interest_.end()) {
    for (const auto& watch : watches_interested->second) {
      ret.insert(watch);
    }
  }
  return ret;
}

void WatchMap::onConfigUpdate(const std::vector<DecodedResourcePtr>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    return;
  }

  // Track any removals triggered by earlier watch updates.
  ASSERT(deferred_removed_during_update_ == nullptr);
  deferred_removed_during_update_ = std::make_unique<absl::flat_hash_set<Watch*>>();
  Cleanup cleanup([this] { removeDeferredWatches(); });
  // The xDS server may send a resource that Envoy isn't interested in. This bit array
  // will hold an "interesting" bit for each of the resources sent in the update.
  std::vector<bool> interesting_resources;
  interesting_resources.reserve(resources.size());
  // Build a map from watches, to the set of updated resources that each watch cares about. Each
  // entry in the map is then a nice little bundle that can be fed directly into the individual
  // onConfigUpdate()s.
  absl::flat_hash_map<Watch*, std::vector<DecodedResourceRef>> per_watch_updates;
  for (const auto& r : resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r = watchesInterestedIn(r->name());
    for (const auto& interested_watch : interested_in_r) {
      per_watch_updates[interested_watch].emplace_back(*r);
    }
    // Set the corresponding interested_resources entry to true iff there is a
    // watch interested in the resource.
    interesting_resources.emplace_back(!interested_in_r.empty());
  }

  // Execute external config validators.
  config_validators_.executeValidators(type_url_, resources);

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
        THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate({}, version_info));
      }
    } else {
      watch->state_of_the_world_empty_ = false;
      THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate(this_watch_updates->second, version_info));
    }
  }

  if (eds_resources_cache_.has_value()) {
    // Add/update the watched resources to/in the cache.
    // Only resources that have a watcher should be updated.
    for (uint32_t resource_idx = 0; resource_idx < resources.size(); ++resource_idx) {
      if (interesting_resources[resource_idx]) {
        const auto& resource = resources[resource_idx];
        const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
            dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
                resource.get()->resource());
        eds_resources_cache_->setResource(resource.get()->name(), cluster_load_assignment);
      }
    }
    // Note: No need to remove resources from the cache, as currently only non-collection
    // subscriptions are supported, and these resources are removed in the call
    // to updateWatchInterest().
  }
}

void WatchMap::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    return;
  }

  std::vector<DecodedResourcePtr> decoded_resources;
  for (const auto& r : resources) {
    decoded_resources.emplace_back(
        DecodedResourceImpl::fromResource((*watches_.begin())->resource_decoder_, r, version_info));
  }

  onConfigUpdate(decoded_resources, version_info);
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
  std::vector<DecodedResourcePtr> decoded_resources;
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

  // Execute external config validators.
  config_validators_.executeValidators(type_url_, decoded_resources, removed_resources);

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& [cur_watch, resource_to_add] : per_watch_added) {
    if (deferred_removed_during_update_->count(cur_watch) > 0) {
      continue;
    }
    const auto removed = per_watch_removed.find(cur_watch);
    if (removed == per_watch_removed.end()) {
      // additions only, no removals
      THROW_IF_NOT_OK(
          cur_watch->callbacks_.onConfigUpdate(resource_to_add, {}, system_version_info));
    } else {
      // both additions and removals
      THROW_IF_NOT_OK(cur_watch->callbacks_.onConfigUpdate(resource_to_add, removed->second,
                                                           system_version_info));
      // Drop the removals now, so the final removals-only pass won't use them.
      per_watch_removed.erase(removed);
    }
  }
  // Any removals-only updates will not have been picked up in the per_watch_added loop.
  for (auto& [cur_watch, resource_to_remove] : per_watch_removed) {
    if (deferred_removed_during_update_->count(cur_watch) > 0) {
      continue;
    }
    THROW_IF_NOT_OK(
        cur_watch->callbacks_.onConfigUpdate({}, resource_to_remove, system_version_info));
  }
  // notify empty update
  if (added_resources.empty() && removed_resources.empty()) {
    for (auto& cur_watch : wildcard_watches_) {
      THROW_IF_NOT_OK(cur_watch->callbacks_.onConfigUpdate({}, {}, system_version_info));
    }
  }

  if (eds_resources_cache_.has_value()) {
    // Add/update the watched resources to/in the cache.
    for (const auto& resource : decoded_resources) {
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
          dynamic_cast<const envoy::config::endpoint::v3::ClusterLoadAssignment&>(
              resource->resource());
      eds_resources_cache_->setResource(resource->name(), cluster_load_assignment);
    }
    // No need to remove resources from the cache, as currently only non-collection
    // subscriptions are supported, and these resources are removed in the call
    // to updateWatchInterest().
  }
}

void WatchMap::onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) {
  for (auto& watch : watches_) {
    watch->callbacks_.onConfigUpdateFailed(reason, e);
  }
}

absl::flat_hash_set<std::string>
WatchMap::findAdditions(const absl::flat_hash_set<std::string>& newly_added_to_watch,
                        Watch* watch) {
  absl::flat_hash_set<std::string> newly_added_to_subscription;
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

absl::flat_hash_set<std::string>
WatchMap::findRemovals(const absl::flat_hash_set<std::string>& newly_removed_from_watch,
                       Watch* watch) {
  absl::flat_hash_set<std::string> newly_removed_from_subscription;
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
