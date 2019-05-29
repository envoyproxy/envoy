#pragma once

#include <set>
#include <string>
#include <utility>

namespace Envoy {
namespace Config {

// Manages "watches" of xDS resources. Several xDS callers might ask for a subscription to the same
// resource name "X". The xDS machinery must return to each their very own subscription to X.
// The xDS machinery's "watch" concept accomplishes that, while avoiding parallel reduntant xDS
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
class WatchMap : public SubscriptionCallbacks {
public:
  // An opaque token given out to users of WatchMap, to identify a given watch.
  using Token = uint64_t;

  // Adds 'callbacks' to the WatchMap, with no resource names being watched.
  // (Use updateWatchInterest() to add some names).
  // Returns a new token identifying the newly added watch.
  Token addWatch(SubscriptionCallbacks& callbacks);

  // Returns true if this was the very last watch in the map.
  // Expects that the watch to be removed has already had all of its resource names removed via
  // updateWatchInterest().
  bool removeWatch(Token token);

  // Updates the set of resource names that the given watch should watch.
  // Returns any resource name additions/removals that are unique across all watches. That is:
  // 1) if 'resources' contains X and no other watch cares about X, X will be in pair.first.
  // 2) if 'resources' does not contain Y, and this watch was the only one that cared about Y,
  //    Y will be in pair.second.
  std::pair<std::set<std::string>, std::set<std::string>>
  updateWatchInterest(Token token, const std::set<std::string>& update_to_these_names);

  // SubscriptionCallbacks
  virtual void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) override;
  virtual void
  onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info) override;

  virtual void onConfigUpdateFailed(const EnvoyException* e) override;

  virtual std::string resourceName(const ProtobufWkt::Any& resource) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

private:
  struct Watch {
    std::set<std::string> resource_names_; // must be sorted set, for set_difference.
    SubscriptionCallbacks& callbacks_;
  };

  // Given a list of names that are new to an individual watch, returns those names that are in fact
  // new to the entire subscription.
  std::set<std::string> findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                      Token token);

  // Given a list of names that an individual watch no longer cares about, returns those names that
  // in fact the entire subscription no longer cares about.
  std::set<std::string> findRemovals(const std::vector<std::string>& newly_removed_from_watch,
                                     Token token);

  // Calls watches_[token].callbacks_.onConfigUpdate(), or logs an error if token isn't in watches_.
  void tryDeliverConfigUpdate(
      Token token, const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& system_version_info);

  // Does a lookup in watch_interest_, returning empty set if not found.
  const absl::flat_hash_set<Token>& tokensInterestedIn(const std::string& resource_name);

  absl::flat_hash_map<Token, Watch> watches_;

  // Maps a resource name to the set of watches interested in that resource. Has two purposes:
  // 1) Acts as a reference count; no watches care anymore ==> the resource can be removed.
  // 2) Enables efficient lookup of all interested watches when a resource has been updated.
  absl::flat_hash_map<std::string, absl::flat_hash_set<Token>> watch_interest_;

  Token next_watch_{0};

  WatchMap(const WatchMap&) = delete;
  WatchMap& operator=(const WatchMap&) = delete;
};

} // namespace Config
} // namespace Envoy
