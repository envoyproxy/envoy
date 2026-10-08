#pragma once

#include <string>
#include <vector>

#include "envoy/config/subscription.h"
#include "envoy/config/xds_manager.h"

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

/**
 * Subscription decorator that pauses the discovery requests of all dependent type URLs (see
 * dependentTypeUrls()) on every xDS mux managed by the XdsManager while a config update is being
 * applied. This batches the requests of the subscriptions created while processing the update
 * (e.g., RDS subscriptions created by an LDS update) into a single request per type, regardless of
 * the transport used by the subscription (gRPC, REST, filesystem) or which mux serves the
 * dependent types.
 */
class DependentTypePausingSubscription : public Subscription, public SubscriptionCallbacks {
public:
  using SubscriptionCreator =
      absl::FunctionRef<absl::StatusOr<SubscriptionPtr>(SubscriptionCallbacks& callbacks)>;

  /**
   * Creates a subscription using `create_subscription`. If `type_url` has dependent type URLs, the
   * created subscription is wrapped so that updates are applied with the dependent types paused.
   * @param xds_manager the manager used to pause the dependent type URLs.
   * @param type_url the type URL of the subscription.
   * @param callbacks the callbacks to be invoked by the subscription.
   * @param create_subscription creates the underlying subscription with the given callbacks.
   */
  static absl::StatusOr<SubscriptionPtr> create(XdsManager& xds_manager, absl::string_view type_url,
                                                SubscriptionCallbacks& callbacks,
                                                SubscriptionCreator create_subscription);

  // Config::Subscription
  void start(const absl::flat_hash_set<std::string>& resource_names) override {
    subscription_->start(resource_names);
  }
  void
  updateResourceInterest(const absl::flat_hash_set<std::string>& update_to_these_names) override {
    subscription_->updateResourceInterest(update_to_these_names);
  }
  void requestOnDemandUpdate(const absl::flat_hash_set<std::string>& add_these_names) override {
    subscription_->requestOnDemandUpdate(add_these_names);
  }

  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override {
    callbacks_.onConfigUpdateFailed(reason, e);
  }

private:
  DependentTypePausingSubscription(XdsManager& xds_manager, SubscriptionCallbacks& callbacks,
                                   const std::vector<std::string>& dependent_type_urls)
      : xds_manager_(xds_manager), callbacks_(callbacks),
        dependent_type_urls_(dependent_type_urls) {}

  XdsManager& xds_manager_;
  SubscriptionCallbacks& callbacks_;
  const std::vector<std::string>& dependent_type_urls_;
  // Set right after construction, as the underlying subscription references this object as its
  // callbacks.
  SubscriptionPtr subscription_;
};

} // namespace Config
} // namespace Envoy
