#pragma once

#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/extensions/config_subscription/grpc/xds_mux/subscription_state.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

// Tracks the state of a "state-of-the-world" (i.e. not delta) xDS-over-gRPC protocol session.
class SotwSubscriptionState
    : public BaseSubscriptionState<envoy::service::discovery::v3::DiscoveryResponse,
                                   envoy::service::discovery::v3::DiscoveryRequest> {
public:
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  SotwSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                        Event::Dispatcher& dispatcher,
                        OpaqueResourceDecoderSharedPtr resource_decoder,
                        XdsConfigTrackerOptRef xds_config_tracker,
                        XdsResourcesDelegateOptRef xds_resources_delegate,
                        const std::string& target_xds_authority);
  ~SotwSubscriptionState() override;

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed) override;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const override;

  void markStreamFresh(bool) override;

  void ttlExpiryCallback(const std::vector<std::string>& expired) override;

  void handleEstablishmentFailure() override;

  SotwSubscriptionState(const SotwSubscriptionState&) = delete;
  SotwSubscriptionState& operator=(const SotwSubscriptionState&) = delete;

private:
  std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>
  getNextRequestInternal() override;

  void handleGoodResponse(const envoy::service::discovery::v3::DiscoveryResponse& message) override;
  void setResourceTtl(const DecodedResourceImpl& decoded_resource);
  bool isHeartbeatResource(const DecodedResource& resource, const std::string& version);

  OpaqueResourceDecoderSharedPtr resource_decoder_;

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

class SotwSubscriptionStateFactory : public SubscriptionStateFactory<SotwSubscriptionState> {
public:
  SotwSubscriptionStateFactory(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  ~SotwSubscriptionStateFactory() override = default;
  std::unique_ptr<SotwSubscriptionState>
  makeSubscriptionState(const std::string& type_url, UntypedConfigUpdateCallbacks& callbacks,
                        OpaqueResourceDecoderSharedPtr resource_decoder,
                        XdsConfigTrackerOptRef xds_config_tracker,
                        XdsResourcesDelegateOptRef xds_resources_delegate,
                        const std::string& target_xds_authority) override {
    return std::make_unique<SotwSubscriptionState>(type_url, callbacks, dispatcher_,
                                                   resource_decoder, xds_config_tracker,
                                                   xds_resources_delegate, target_xds_authority);
  }

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
