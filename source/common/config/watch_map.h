#pragma once

#include <set>
#include <string>
#include <utility>

#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Config {

struct Watch;
using WatchPtr = std::unique_ptr<Watch>;

struct AddedRemoved {
  AddedRemoved(std::set<std::string> added, std::set<std::string> removed)
      : added_(added), removed_(removed) {}
  std::set<std::string> added_;
  std::set<std::string> removed_;
};

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
class WatchMap : public SubscriptionCallbacks, public Logger::Loggable<Logger::Id::config> {
public:
  WatchMap() {}

  // Adds 'callbacks' to the WatchMap, with no resource names being watched.
  // (Use updateWatchInterest() to add some names).
  // Returns the newly added watch, to be used for updateWatchInterest. Destroy to remove from map.
  WatchPtr addWatch(SubscriptionCallbacks& callbacks);

  // Updates the set of resource names that the given watch should watch.
  // Returns any resource name additions/removals that are unique across all watches. That is:
  // 1) if 'resources' contains X and no other watch cares about X, X will be in added_.
  // 2) if 'resources' does not contain Y, and this watch was the only one that cared about Y,
  //    Y will be in removed_.
  AddedRemoved updateWatchInterest(Watch* watch,
                                   const std::set<std::string>& update_to_these_names);

  // SubscriptionCallbacks
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) override;
  virtual void
  onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) override;

  virtual void onConfigUpdateFailed(const EnvoyException* e) override;

  virtual std::string resourceName(const ProtobufWkt::Any&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  friend struct Watch;
  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  void removeWatch(Watch* watch);

  // Given a list of names that are new to an individual watch, returns those names that are in fact
  // new to the entire subscription.
  std::set<std::string> findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                      Watch* watch);

  // Given a list of names that an individual watch no longer cares about, returns those names that
  // in fact the entire subscription no longer cares about.
  std::set<std::string> findRemovals(const std::vector<std::string>& newly_removed_from_watch,
                                     Watch* watch);

  // Returns the union of watch_interest_[resource_name] and wildcard_watches_.
  absl::flat_hash_set<Watch*> watchesInterestedIn(const std::string& resource_name);

  absl::flat_hash_set<Watch*> watches_;

  // Watches whose interest set is currently empty, which is interpreted as "everything".
  absl::flat_hash_set<Watch*> wildcard_watches_;

  // Maps a resource name to the set of watches interested in that resource. Has two purposes:
  // 1) Acts as a reference count; no watches care anymore ==> the resource can be removed.
  // 2) Enables efficient lookup of all interested watches when a resource has been updated.
  absl::flat_hash_map<std::string, absl::flat_hash_set<Watch*>> watch_interest_;

  WatchMap(const WatchMap&) = delete;
  WatchMap& operator=(const WatchMap&) = delete;
};

struct Watch {
  Watch(WatchMap& owning_map, SubscriptionCallbacks& callbacks)
      : owning_map_(owning_map), callbacks_(callbacks) {}
  ~Watch() { owning_map_.removeWatch(this); }
  WatchMap& owning_map_;
  SubscriptionCallbacks& callbacks_;
  std::set<std::string> resource_names_; // must be sorted set, for set_difference.
};

} // namespace Config
} // namespace Envoy
