#pragma once

#include <set>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

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

class WatchMap {
public:
  virtual ~WatchMap() = default;

  // Adds 'callbacks' to the WatchMap, with no resource names being watched.
  // (Use updateWatchInterest() to add some names).
  // Returns the newly added watch, to be used for updateWatchInterest. Destroy to remove from map.
  virtual WatchPtr addWatch(SubscriptionCallbacks& callbacks) PURE;

  // Updates the set of resource names that the given watch should watch.
  // Returns any resource name additions/removals that are unique across all watches. That is:
  // 1) if 'resources' contains X and no other watch cares about X, X will be in added_.
  // 2) if 'resources' does not contain Y, and this watch was the only one that cared about Y,
  //    Y will be in removed_.
  virtual AddedRemoved updateWatchInterest(Watch* watch,
                                           const std::set<std::string>& update_to_these_names) PURE;

  // Intended to be called by the Watch's destructor.
  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  virtual void removeWatch(Watch* watch) PURE;
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
