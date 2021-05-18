#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/grpc/status.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/config/unified_mux/subscription_state.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace UnifiedMux {

// Tracks the state of a "state-of-the-world" (i.e. not delta) xDS-over-gRPC protocol session.
class SotwSubscriptionState
    : public SubscriptionState<envoy::service::discovery::v3::DiscoveryResponse,
                               envoy::service::discovery::v3::DiscoveryRequest> {
public:
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  SotwSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout,
                        Event::Dispatcher& dispatcher);
  ~SotwSubscriptionState() override;

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed) override;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const override;

  void markStreamFresh() override;

  void ttlExpiryCallback(const std::vector<std::string>& expired) override;

  SotwSubscriptionState(const SotwSubscriptionState&) = delete;
  SotwSubscriptionState& operator=(const SotwSubscriptionState&) = delete;

private:
  std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>
  getNextRequestInternal() override;

  void handleGoodResponse(const envoy::service::discovery::v3::DiscoveryResponse& message) override;
  bool isHeartbeatResource(const envoy::service::discovery::v3::Resource& resource,
                           const std::string& version);

  // The version_info carried by the last accepted DiscoveryResponse.
  // Remains empty until one is accepted.
  absl::optional<std::string> last_good_version_info_;
  // The nonce carried by the last accepted DiscoveryResponse.
  // Remains empty until one is accepted.
  // Used when it's time to make a spontaneous (i.e. not primarily meant as an ACK) request.
  absl::optional<std::string> last_good_nonce_;

  // Starts true because we should send a request upon subscription start.
  bool update_pending_{true};

  absl::flat_hash_set<std::string> names_tracked_;
};

} // namespace UnifiedMux
} // namespace Config
} // namespace Envoy
