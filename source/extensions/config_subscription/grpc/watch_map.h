#pragma once

#include <set>
#include <string>
#include <utility>

#include "envoy/config/custom_config_validators.h"
#include "envoy/config/eds_resources_cache.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/resource_name.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Config {

struct AddedRemoved {
  AddedRemoved(absl::flat_hash_set<std::string>&& added, absl::flat_hash_set<std::string>&& removed)
      : added_(std::move(added)), removed_(std::move(removed)) {}
  absl::flat_hash_set<std::string> added_;
  absl::flat_hash_set<std::string> removed_;
};

struct Watch {
  Watch(SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder)
      : callbacks_(callbacks), resource_decoder_(resource_decoder) {}
  SubscriptionCallbacks& callbacks_;
  OpaqueResourceDecoder& resource_decoder_;
  absl::flat_hash_set<std::string> resource_names_;
  // Needed only for state-of-the-world.
  // Whether the most recent update contained any resources this watch cares about.
  // If true, a new update that also contains no resources can skip this watch.
  bool state_of_the_world_empty_{true};
};

// NOTE: Users are responsible for eventually calling removeWatch() on the Watch* returned
//       by addWatch(). We don't expect there to be new users of this class beyond
//       NewGrpcMuxImpl and DeltaSubscriptionImpl (TODO(fredlas) to be renamed).
//
// Manages "watches" of xDS resources. Several xDS callers might ask for a subscription to the same
// resource name "X". The xDS machinery must return to each their very own subscription to X.
// The xDS machinery's "watch" concept accomplishes that, while avoiding parallel redundant xDS
// requests for X. Each of those subscriptions is viewed as a "watch" on X, while behind the scenes
// there is just a single real subscription to that resource name.
//
// This class maintains the watches<-->subscription mapping: it
// 1) delivers updates to all interested watches, and
// 2) tracks which resource names should be {added to,removed from} the subscription when the
//    {first,last} watch on a resource name is {added,removed}.
//
// #1 is accomplished by WatchMap's implementation of the SubscriptionCallbacks interface.
// This interface allows the xDS client to just throw each xDS update message it receives directly
// into WatchMap::onConfigUpdate, rather than having to track the various watches' callbacks.
//
// The information for #2 is returned by updateWatchInterest(); the caller should use it to
// update the subscription accordingly.
//
// A WatchMap is assumed to be dedicated to a single type_url type of resource (EDS, CDS, etc).
//
// The WatchMap can also store the fetched resources in a cache, and allow others to fetch
// resources directly from the cache. This is done for EDS in the following case:
// Assume an active EDS cluster exists with some load-assignment that is kept in the cache.
// If the cluster is updated, and no load-assignment is sent from the xDS server, the
// cached version will be used.
// The WatchMap is responsible to update the cache with the resource contents, and it is
// up to the specific xDS type subscription handler (i.e., EdsClusterImpl), to fetch
// the resource from the cache.
class WatchMap : public UntypedConfigUpdateCallbacks, public Logger::Loggable<Logger::Id::config> {
public:
  WatchMap(const bool use_namespace_matching, const std::string& type_url,
           CustomConfigValidators* config_validators, EdsResourcesCacheOptRef eds_resources_cache)
      : use_namespace_matching_(use_namespace_matching), type_url_(type_url),
        config_validators_(config_validators), eds_resources_cache_(eds_resources_cache) {
    // If eds resources cache is provided, then the type must be ClusterLoadAssignment.
    ASSERT(!eds_resources_cache_.has_value() ||
           (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>()));
  }

  // Adds 'callbacks' to the WatchMap, with every possible resource being watched.
  // (Use updateWatchInterest() to narrow it down to some specific names).
  // Returns the newly added watch, to be used with updateWatchInterest and removeWatch.
  Watch* addWatch(SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder);

  // Updates the set of resource names that the given watch should watch.
  // Returns any resource name additions/removals that are unique across all watches. That is:
  // 1) if 'resources' contains X and no other watch cares about X, X will be in added_.
  // 2) if 'resources' does not contain Y, and this watch was the only one that cared about Y,
  //    Y will be in removed_.
  AddedRemoved updateWatchInterest(Watch* watch,
                                   const absl::flat_hash_set<std::string>& update_to_these_names);

  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  void removeWatch(Watch* watch);

  // UntypedConfigUpdateCallbacks.
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;

  void onConfigUpdate(const std::vector<DecodedResourcePtr>& resources,
                      const std::string& version_info) override;

  void
  onConfigUpdate(absl::Span<const envoy::service::discovery::v3::Resource* const> added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) override;
  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override;

  WatchMap(const WatchMap&) = delete;
  WatchMap& operator=(const WatchMap&) = delete;

  void setConfigValidators(CustomConfigValidators* config_validators) {
    config_validators_ = config_validators;
  }

private:
  void removeDeferredWatches();

  // Given a list of names that are new to an individual watch, returns those names that are in fact
  // new to the entire subscription.
  absl::flat_hash_set<std::string>
  findAdditions(const absl::flat_hash_set<std::string>& newly_added_to_watch, Watch* watch);

  // Given a list of names that an individual watch no longer cares about, returns those names that
  // in fact the entire subscription no longer cares about.
  absl::flat_hash_set<std::string>
  findRemovals(const absl::flat_hash_set<std::string>& newly_removed_from_watch, Watch* watch);

  // Returns the union of watch_interest_[resource_name] and wildcard_watches_.
  absl::flat_hash_set<Watch*> watchesInterestedIn(const std::string& resource_name);

  absl::flat_hash_set<std::unique_ptr<Watch>> watches_;

  // Watches whose interest set is currently empty, which is interpreted as "everything".
  absl::flat_hash_set<Watch*> wildcard_watches_;

  // Watches that have been removed inside the call stack of the WatchMap's onConfigUpdate(). This
  // can happen when a watch's onConfigUpdate() results in another watch being removed via
  // removeWatch().
  std::unique_ptr<absl::flat_hash_set<Watch*>> deferred_removed_during_update_;

  // Maps a resource name to the set of watches interested in that resource. Has two purposes:
  // 1) Acts as a reference count; no watches care anymore ==> the resource can be removed.
  // 2) Enables efficient lookup of all interested watches when a resource has been updated.
  absl::flat_hash_map<std::string, absl::flat_hash_set<Watch*>> watch_interest_;

  const bool use_namespace_matching_;
  const std::string type_url_;
  CustomConfigValidators* config_validators_;
  EdsResourcesCacheOptRef eds_resources_cache_;
};

} // namespace Config
} // namespace Envoy
