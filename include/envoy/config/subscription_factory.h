#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
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
   * @param resource_decoder how incoming opaque resource objects are to be decoded.
   *
   * @return SubscriptionPtr subscription object corresponding for config and type_url.
   */
  virtual SubscriptionPtr
  subscriptionFromConfigSource(const envoy::config::core::v3::ConfigSource& config,
                               absl::string_view type_url, Stats::Scope& scope,
                               SubscriptionCallbacks& callbacks,
                               OpaqueResourceDecoder& resource_decoder) PURE;
};

} // namespace Config
} // namespace Envoy
