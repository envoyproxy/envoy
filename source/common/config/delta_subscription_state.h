#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/local_info/local_info.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Config {

struct UpdateAck {
  UpdateAck(absl::string_view nonce) : nonce_(nonce) {}
  std::string nonce_;
  ::google::rpc::Status error_detail_;
};

// Tracks the xDS protocol state of an individual ongoing delta xDS session.
class DeltaSubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionState(const std::string& type_url, const std::set<std::string>& resource_names,
                         SubscriptionCallbacks& callbacks, const LocalInfo::LocalInfo& local_info,
                         std::chrono::milliseconds init_fetch_timeout,
                         Event::Dispatcher& dispatcher, SubscriptionStats& stats);

  void setInitFetchTimeout(Event::Dispatcher& dispatcher);

  void pause();
  void resume();
  bool paused() const { return paused_; }

  // Update which resources we're interested in subscribing to.
  void updateResourceInterest(const std::set<std::string>& update_to_these_names);

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const;

  void markStreamFresh() { any_request_sent_yet_in_current_stream_ = false; }

  UpdateAck handleResponse(const envoy::api::v2::DeltaDiscoveryResponse& message);

  void handleEstablishmentFailure();

  envoy::api::v2::DeltaDiscoveryRequest getNextRequest();

private:
  void handleGoodResponse(const envoy::api::v2::DeltaDiscoveryResponse& message);
  void handleBadResponse(const EnvoyException& e, UpdateAck& ack);
  void disableInitFetchTimeoutTimer();

  class ResourceVersion {
  public:
    explicit ResourceVersion(absl::string_view version) : version_(version) {}
    // Builds a ResourceVersion in the waitingForServer state.
    ResourceVersion() {}

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool waitingForServer() const { return version_ == absl::nullopt; }
    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
  };

  // Use these helpers to ensure resource_versions_ and resource_names_ get updated together.
  void setResourceVersion(const std::string& resource_name, const std::string& resource_version);
  void setResourceWaitingForServer(const std::string& resource_name);
  void setLostInterestInResource(const std::string& resource_name);

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  std::unordered_map<std::string, ResourceVersion> resource_versions_;
  // The keys of resource_versions_. Only tracked separately because std::map does not provide an
  // iterator into just its keys, e.g. for use in std::set_difference.
  std::set<std::string> resource_names_;

  const std::string type_url_;
  SubscriptionCallbacks& callbacks_;
  const LocalInfo::LocalInfo& local_info_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;

  bool paused_{};
  bool any_request_sent_yet_in_current_stream_{};

  // Tracks changes in our subscription interest since the previous DeltaDiscoveryRequest we sent.
  // Can't use unordered_set due to ordering issues in gTest expectation matching.
  // Feel free to change to unordered if you can figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;

  SubscriptionStats& stats_;
};

} // namespace Config
} // namespace Envoy
