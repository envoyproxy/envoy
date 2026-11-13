#include "source/extensions/config_subscription/grpc/watch_map.h"

#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/runtime/runtime_features.h"

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

void WatchMap::forgetWatch(Watch* watch) {
  wildcard_watches_.erase(watch); // may or may not be in there, but we want it gone.
  forgetAccept(watch);
  // Drop the queued replay so the pointer is not kept after the watch is freed below; a new
  // watch allocated at the same address must not inherit it.
  pending_replays_.erase(watch);
  watches_.erase(watch);
}

bool WatchMap::watchTrackedInInterest(Watch* watch) {
  for (const auto& [name, watches] : watch_interest_) {
    if (watches.contains(watch)) {
      return true;
    }
  }
  return false;
}

void WatchMap::forgetAccept(Watch* watch) {
  // Remove this watch from the glob reverse-index, using its own record of accepted patterns.
  for (absl::string_view pattern : watch->accept_patterns_) {
    if (pattern == Wildcard) {
      accept_wildcard_watches_.erase(watch);
      continue;
    }
    // "<prefix>/*" -- keyed by "<prefix>".
    auto it = accept_prefix_watches_.find(pattern.substr(0, pattern.size() - 2));
    if (it != accept_prefix_watches_.end()) {
      it->second.erase(watch);
      if (it->second.empty()) {
        accept_prefix_watches_.erase(it);
      }
    }
  }
}

void WatchMap::updateWildcardWatches(Watch* watch) {
  // accept() patterns are evaluated FIRST and take precedence: a watch that has declared any
  // accept() pattern is routed by those patterns (plus its concrete names) only, so it is never a
  // catch-all wildcard watch here -- even if its resource_names_ is empty or contains "*". This is
  // what lets a watch subscribe to the wildcard on the wire while routing only a subset (e.g.
  // VHDS). To keep wildcard routing while using accept(), the caller includes "*" in the accept()
  // patterns, which is tracked separately in accept_wildcard_watches_.
  if (!watch->accept_patterns_.empty()) {
    wildcard_watches_.erase(watch);
    return;
  }
  // No accept() filter: the watched set defines routing. An explicit "*" or an empty watched set
  // makes the watch a catch-all wildcard; otherwise it is routed only its specific resource names.
  if (watch->resource_names_.contains(Wildcard) || watch->resource_names_.empty()) {
    wildcard_watches_.insert(watch);
  } else {
    wildcard_watches_.erase(watch);
  }
}

void WatchMap::removeWatch(Watch* watch) {
  // Contract: the caller must first empty this watch's resource-name interest via
  // updateWatchInterest(watch, {}) so that the per-name reference counts in watch_interest_ are
  // decremented and any resulting server-side unsubscribe is emitted (see findRemovals()). Removal
  // here only cleans the membership structures (wildcard_watches_, the accept_*
  // indices, watches_); it deliberately does not touch watch_interest_. If the watch is still
  // referenced by any watch_interest_ entry, a resource name was leaked (and the server never told
  // to unsubscribe it), so fail loudly in debug builds.
  ASSERT(!watchTrackedInInterest(watch),
         "removeWatch() called before the watch's resource names were drained via "
         "updateWatchInterest(watch, {})");
  if (deferred_removed_during_update_ != nullptr) {
    deferred_removed_during_update_->insert(watch);
  } else {
    forgetWatch(watch);
  }
}

void WatchMap::removeDeferredWatches() {
  for (auto* watch : *deferred_removed_during_update_) {
    forgetWatch(watch);
  }
  deferred_removed_during_update_ = nullptr;
}

AddedRemoved
WatchMap::updateWatchInterest(Watch* watch,
                              const absl::flat_hash_set<std::string>& update_to_these_names) {
  absl::flat_hash_set<std::string> newly_added_to_watch;
  SetUtil::setDifference(update_to_these_names, watch->resource_names_, newly_added_to_watch);

  absl::flat_hash_set<std::string> newly_removed_from_watch;
  SetUtil::setDifference(watch->resource_names_, update_to_these_names, newly_removed_from_watch);

  watch->resource_names_ = update_to_these_names;
  updateWildcardWatches(watch);

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

AddedRemoved
WatchMap::updateWatchInterest(Watch* watch,
                              const absl::flat_hash_set<std::string>& resources_to_add,
                              const absl::flat_hash_set<std::string>& resources_to_remove) {
  absl::flat_hash_set<std::string> newly_added_to_watch;
  for (const auto& name : resources_to_add) {
    if (!resources_to_remove.contains(name) && watch->resource_names_.insert(name).second) {
      newly_added_to_watch.insert(name);
    }
  }

  absl::flat_hash_set<std::string> newly_removed_from_watch;
  for (const auto& name : resources_to_remove) {
    if (watch->resource_names_.erase(name) > 0) {
      newly_removed_from_watch.insert(name);
    }
  }

  updateWildcardWatches(watch);

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

void WatchMap::accept(Watch* watch, const absl::flat_hash_set<std::string>& patterns) {
  // Forget any previous other-resource interest, so we can precisely update the reverse-index.
  forgetAccept(watch);
  watch->accept_patterns_.clear();

  for (absl::string_view pattern : patterns) {
    if (pattern == Wildcard) {
      accept_wildcard_watches_.insert(watch);
    } else if (absl::EndsWith(pattern, "/*")) {
      // A "<prefix>/*" glob: accept every resource named "<prefix>/...". Keyed by "<prefix>".
      accept_prefix_watches_[pattern.substr(0, pattern.size() - 2)].insert(watch);
    } else {
      IS_ENVOY_BUG("accept() requires the '*' wildcard or a '<prefix>/*' glob");
      continue;
    }
    // Record the pattern on the watch itself (forward index), so it can be cleaned up precisely.
    watch->accept_patterns_.insert(std::string(pattern));
  }

  updateWildcardWatches(watch);
}

absl::flat_hash_set<Watch*> WatchMap::watchesInterestedIn(const std::string& resource_name) {
  absl::flat_hash_set<Watch*> ret = wildcard_watches_;
  // Watches that accepted every other resource via accept("*").
  ret.insert(accept_wildcard_watches_.begin(), accept_wildcard_watches_.end());
  // Watches that accepted this resource's prefix via accept("<prefix>/*").
  if (!accept_prefix_watches_.empty()) {
    const std::string prefix = namespaceFromName(resource_name);
    if (!prefix.empty()) {
      if (auto it = accept_prefix_watches_.find(prefix); it != accept_prefix_watches_.end()) {
        ret.insert(it->second.begin(), it->second.end());
      }
    }
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
    THROW_IF_NOT_OK_REF(resource_or_error.status());
    xdstp_resource = resource_or_error.value();
  }
  auto watches_interested = watch_interest_.find(
      is_xdstp ? XdsResourceIdentifier::encodeUrn(xdstp_resource, encode_options) : resource_name);
  // If that fails, consider namespace/glob matching. This is the slow path for xdstp:// and should
  // only happen for glob collections. TODO(htuch): It should be possible to have much more
  // efficient matchers here.
  if (watches_interested == watch_interest_.end()) {
    if (is_xdstp) {
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
  config_validators_->executeValidators(type_url_, resources);

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
        watch->notified_ = true;
        THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate({}, version_info));
      }
    } else {
      watch->state_of_the_world_empty_ = false;
      watch->notified_ = true;
      THROW_IF_NOT_OK(watch->callbacks_.onConfigUpdate(this_watch_updates->second, version_info));
    }
  }

  // A name absent from a state-of-the-world response is not evicted: for explicitly named
  // types (EDS, RDS, ...) the server may legitimately send a subset, and the watches that
  // already hold the resource keep it too. The cache follows what those watches hold.
  if (replay_enabled_) {
    for (uint32_t resource_idx = 0; resource_idx < resources.size(); ++resource_idx) {
      if (interesting_resources[resource_idx]) {
        cacheResource(*resources[resource_idx], version_info);
      }
    }
  }

  if (eds_resources_cache_.has_value()) {
    // Add/update the watched resources to/in the cache.
    // Only resources that have a watcher should be updated.
    for (uint32_t resource_idx = 0; resource_idx < resources.size(); ++resource_idx) {
      if (interesting_resources[resource_idx]) {
        const auto& resource = resources[resource_idx];
        const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
            Envoy::Protobuf::DynamicCastMessage<envoy::config::endpoint::v3::ClusterLoadAssignment>(
                resource.get()->resource());
        eds_resources_cache_->setResource(resource.get()->name(), cluster_load_assignment);
      }
    }
    // Note: No need to remove resources from the cache, as currently only non-collection
    // subscriptions are supported, and these resources are removed in the call
    // to updateWatchInterest().
  }
}

void WatchMap::onConfigUpdate(const Protobuf::RepeatedPtrField<Protobuf::Any>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    return;
  }

  OpaqueResourceDecoder& resource_decoder = (*watches_.begin())->resource_decoder_;
  Protobuf::Arena batch_arena(resource_decoder.arenaOptions());
  std::vector<DecodedResourcePtr> decoded_resources;
  decoded_resources.reserve(resources.size());
  for (const auto& r : resources) {
    decoded_resources.emplace_back(THROW_OR_RETURN_VALUE(
        DecodedResourceImpl::fromResource(resource_decoder, r, version_info, batch_arena),
        DecodedResourceImplPtr));
  }

  onConfigUpdate(decoded_resources, version_info);
}

void WatchMap::onConfigUpdate(
    absl::Span<const envoy::service::discovery::v3::Resource* const> added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  // Track any removals triggered by earlier watch updates.
  ASSERT(deferred_removed_during_update_ == nullptr);
  deferred_removed_during_update_ = std::make_unique<absl::flat_hash_set<Watch*>>();
  Cleanup cleanup([this] { removeDeferredWatches(); });
  // Build a pair of maps: from watches, to the set of resources {added,removed} that each watch
  // cares about. Each entry in the map-pair is then a nice little bundle that can be fed directly
  // into the individual onConfigUpdate()s.
  Protobuf::Arena batch_arena(watches_.empty()
                                  ? Protobuf::ArenaOptions{}
                                  : (*watches_.begin())->resource_decoder_.arenaOptions());
  std::vector<DecodedResourcePtr> decoded_resources;
  absl::flat_hash_map<Watch*, std::vector<DecodedResourceRef>> per_watch_added;
  // If the server behaves according to the protocol, it will only send the
  // resources the watch map is interested in. Reserve the correct amount of
  // space for the vector for the good case.
  decoded_resources.reserve(added_resources.size());
  for (const auto* r : added_resources) {
    const absl::flat_hash_set<Watch*>& interested_in_r = watchesInterestedIn(r->name());
    // If there are no watches, then we don't need to decode. If there are watches, they should all
    // be for the same resource type, so we can just use the callbacks of the first watch to decode.
    if (interested_in_r.empty()) {
      continue;
    }
    decoded_resources.emplace_back(
        new DecodedResourceImpl((*interested_in_r.begin())->resource_decoder_, *r, batch_arena));
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
  config_validators_->executeValidators(type_url_, decoded_resources, removed_resources);

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& [cur_watch, resource_to_add] : per_watch_added) {
    if (deferred_removed_during_update_->count(cur_watch) > 0) {
      continue;
    }
    cur_watch->notified_ = true;
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
    cur_watch->notified_ = true;
    THROW_IF_NOT_OK(
        cur_watch->callbacks_.onConfigUpdate({}, resource_to_remove, system_version_info));
  }
  // notify empty update
  if (added_resources.empty() && removed_resources.empty()) {
    for (auto& cur_watch : wildcard_watches_) {
      cur_watch->notified_ = true;
      THROW_IF_NOT_OK(cur_watch->callbacks_.onConfigUpdate({}, {}, system_version_info));
    }
  }

  if (replay_enabled_) {
    for (const auto& resource : decoded_resources) {
      cacheResource(*resource, resource->version());
    }
    for (const auto& name : removed_resources) {
      cached_resources_.erase(name);
    }
  }

  if (eds_resources_cache_.has_value()) {
    // Add/update the watched resources to/in the cache.
    for (const auto& resource : decoded_resources) {
      const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
          Envoy::Protobuf::DynamicCastMessage<envoy::config::endpoint::v3::ClusterLoadAssignment>(
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
      // Add this watch to the already-existing set at watch_interest_[name]. The name is already
      // subscribed, so the server will not send the resource again for this watch.
      entry->second.insert(watch);
      queueReplay(watch, name);
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
      cached_resources_.erase(name);
      newly_removed_from_subscription.insert(name);
    }
  }
  // A watch that dropped a name before its replay ran must not receive it.
  if (auto pending = pending_replays_.find(watch); pending != pending_replays_.end()) {
    for (const auto& name : newly_removed_from_watch) {
      pending->second.erase(name);
    }
    if (pending->second.empty()) {
      pending_replays_.erase(pending);
    }
  }
  return newly_removed_from_subscription;
}

const std::string& WatchMap::secretTypeUrl() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>());
}

void WatchMap::cacheResource(const DecodedResource& resource, const std::string& version) {
  // Only explicitly named interest is cached: a resource routed through a wildcard or glob
  // watch has no per-name reference count, and a wildcard subscription is resent in full anyway.
  if (!resource.hasResource() || !watch_interest_.contains(resource.name())) {
    return;
  }
  Protobuf::Arena::UniquePtr<Protobuf::Message> copy =
      Protobuf::Arena::UnsafeWrapUniquePtr(nullptr, resource.resource().New());
  copy->CopyFrom(resource.resource());
  cached_resources_.insert_or_assign(resource.name(),
                                     CachedResource{std::move(copy), resource.aliases(), version});
}

void WatchMap::queueReplay(Watch* watch, const std::string& name) {
  // A watch that has already been notified holds a state built from server responses; only a
  // watch that has never heard anything is caught up from the cache.
  if (!replay_enabled_ || watch->notified_ || !cached_resources_.contains(name) ||
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.xds_replay_cached_resources")) {
    return;
  }
  pending_replays_[watch].insert(name);
  replay_cb_->scheduleCallbackNextIteration();
}

void WatchMap::replayCachedResources() {
  // Take the queue first: a delivery may add or remove watches, and must not disturb this pass.
  auto pending = std::move(pending_replays_);
  pending_replays_.clear();
  for (auto& [watch, names] : pending) {
    // The watch may have been removed since the replay was queued, or a server response may
    // have reached it first, in which case the response wins.
    if (!watches_.contains(watch) || watch->notified_) {
      continue;
    }
    std::vector<DecodedResourcePtr> decoded;
    std::vector<DecodedResourceRef> refs;
    std::string version;
    for (const auto& name : names) {
      // The entry may be gone if a delta removal followed the join, e.g. a watch added from
      // inside the removal's own callbacks, which are not told about the removal. The interest
      // check is defensive: findRemovals() already drops a name the watch stopped watching.
      auto cached = cached_resources_.find(name);
      if (cached == cached_resources_.end() || !watch->resource_names_.contains(name)) {
        continue;
      }
      Protobuf::Arena::UniquePtr<Protobuf::Message> copy =
          Protobuf::Arena::UnsafeWrapUniquePtr(nullptr, cached->second.resource_->New());
      copy->CopyFrom(*cached->second.resource_);
      decoded.push_back(std::make_unique<DecodedResourceImpl>(
          std::move(copy), name, cached->second.aliases_, cached->second.version_));
      refs.emplace_back(*decoded.back());
      version = cached->second.version_;
    }
    if (refs.empty()) {
      continue;
    }
    ENVOY_LOG(debug, "Replaying {} cached {} resource(s) to a late watch", refs.size(), type_url_);
    // The resource was accepted when it arrived, and there is no response to NACK, so a failure
    // here is only logged. The watch keeps waiting for the server as it would without replay.
    watch->notified_ = true;
    TRY_ASSERT_MAIN_THREAD {
      absl::Status status;
      if (is_delta_) {
        status = watch->callbacks_.onConfigUpdate(refs, {}, version);
      } else {
        // The watch now holds resources, so a later state-of-the-world update that drops them
        // must be delivered to it (see the delivery loop in onConfigUpdate()).
        watch->state_of_the_world_empty_ = false;
        status = watch->callbacks_.onConfigUpdate(refs, version);
      }
      if (!status.ok()) {
        ENVOY_LOG(warn, "Replay of cached {} resources rejected: {}", type_url_, status.message());
      }
    }
    END_TRY
    CATCH(const EnvoyException& e,
          { ENVOY_LOG(warn, "Replay of cached {} resources failed: {}", type_url_, e.what()); });
  }
}

} // namespace Config
} // namespace Envoy
