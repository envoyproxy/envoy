#pragma once

#include <set>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

// Watch and WatchMap together manage "watches" of xDS resources. Several callers might ask
// for subscriptions to the same xDS resource "X". The xDS machinery must give each their
// very own Subscription object that receives updates on X, but we can't be sending multiple
// redundant requests to the server. Watch+WatchMap avoid that: each of those Subscriptions
// just holds a Watch on X; behind the scenes, GrpcMux (instructed by WatchMap) manages the
// actual xDS protocol requests for X.
//
// All of this is implicitly within the context of a given type_url (EDS, CDS, etc), and unaware
// of the watches for the other type_urls.

// pair<set<string>, set<string>>, but with meaningful field names.
struct AddedRemoved {
  AddedRemoved(std::set<std::string>&& added, std::set<std::string>&& removed)
      : added_(std::move(added)), removed_(std::move(removed)) {}
  std::set<std::string> added_;
  std::set<std::string> removed_;
};

// A Watch object tracks the xDS resource names that some object in the wider Envoy codebase is
// interested in. The union of these names becomes the xDS subscription interest.
class Watch : public SubscriptionCallbacks {
public:
  virtual ~Watch() = default;

  // Informs the parent WatchMap of an update to this Watch's set of watched resource names.
  // The resource names in the returned AddedRemoved should be added to/removed from the actual
  // conversation with the xDS server.
  virtual AddedRemoved updateWatchInterest(const std::set<std::string>& update_to_these_names) PURE;
};
using WatchPtr = std::unique_ptr<Watch>;

// WatchMap tracks all of the Watches for a given type_url. When an individual Watch's interest
// changes, its parent WatchMap records the change, and determines what (if any) change to the
// overall xDS subscription interest is needed, based on all other Watches' interests.
class WatchMap {
public:
  virtual ~WatchMap() = default;

  // Adds 'callbacks' to the WatchMap as a wildcard watch. You can later call
  // Watch::updateWatchInterest() to replace the wildcard matching with specific names.
  // Returns ownership of the newly added watch. Destroy to remove from map.
  virtual WatchPtr addWatch(SubscriptionCallbacks& callbacks) PURE;

  // Intended to be called only by the Watch's destructor.
  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  virtual void removeWatch(Watch* watch) PURE;

  // While set to true (which is the default state of a newly added Watch), 'watch' will receive
  // all resource updates in each new config update message.
  virtual void setWildcardness(Watch* watch, bool is_wildcard) PURE;

  // Given a list of names that are new to an individual watch, returns those names that are in fact
  // new to the entire subscription.
  virtual std::set<std::string> findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                              Watch* watch) PURE;

  // Given a list of names that an individual watch no longer cares about, returns those names that
  // in fact the entire subscription no longer cares about.
  virtual std::set<std::string>
  findRemovals(const std::vector<std::string>& newly_removed_from_watch, Watch* watch) PURE;
};

} // namespace Config
} // namespace Envoy
