#pragma once

#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Config {

class SubscriptionFactory {
public:
  virtual ~SubscriptionFactory() = default;

  /**
   * Subscription factory interface.
   *
   * @param config envoy::api::v2::core::ConfigSource to construct from.
   * @param type_url type URL for the resource being subscribed to.
   * @param scope stats scope for any stats tracked by the subscription.
   * @param callbacks the callbacks needed by all Subscription objects, to deliver config updates.
   *                  The callbacks must not result in the deletion of the Subscription object.
   * @return SubscriptionPtr subscription object corresponding for config and type_url.
   */
  virtual SubscriptionPtr
  subscriptionFromConfigSource(const envoy::api::v2::core::ConfigSource& config,
                               absl::string_view type_url, Stats::Scope& scope,
                               SubscriptionCallbacks& callbacks) PURE;
};

} // namespace Config
} // namespace Envoy
