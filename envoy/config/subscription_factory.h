#pragma once

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

#include "xds/core/v3/resource_locator.pb.h"

namespace Envoy {
namespace Config {

class SubscriptionFactory {
public:
  virtual ~SubscriptionFactory() = default;

  /**
   * @return true if a config source comes from the local filesystem.
   */
  static bool
  isPathBasedConfigSource(envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase type) {
    return type == envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath ||
           type ==
               envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPathConfigSource;
  }

  /**
   * Subscription factory interface.
   *
   * @param config envoy::config::core::v3::ConfigSource to construct from.
   * @param type_url type URL for the resource being subscribed to.
   * @param scope stats scope for any stats tracked by the subscription.
   * @param callbacks the callbacks needed by all Subscription objects, to deliver config updates.
   *                  The callbacks must not result in the deletion of the Subscription object.
   * @param resource_decoder how incoming opaque resource objects are to be decoded.
   * @param options subscription options.
   *
   * @return SubscriptionPtr subscription object corresponding for config and type_url.
   */
  virtual SubscriptionPtr subscriptionFromConfigSource(
      const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
      Stats::Scope& scope, SubscriptionCallbacks& callbacks,
      OpaqueResourceDecoderSharedPtr resource_decoder, const SubscriptionOptions& options) PURE;

  /**
   * Collection subscription factory interface for xDS-TP URLs.
   *
   * @param collection_locator collection resource locator.
   * @param config envoy::config::core::v3::ConfigSource for authority resolution.
   * @param type_url type URL for the resources inside the collection.
   * @param scope stats scope for any stats tracked by the subscription.
   * @param callbacks the callbacks needed by all [Collection]Subscription objects, to deliver
   *                  config updates. The callbacks must not result in the deletion of the
   *                  CollectionSubscription object.
   * @param resource_decoder how incoming opaque resource objects are to be decoded.
   *
   * @return SubscriptionPtr subscription object corresponding for collection_locator.
   */
  virtual SubscriptionPtr
  collectionSubscriptionFromUrl(const xds::core::v3::ResourceLocator& collection_locator,
                                const envoy::config::core::v3::ConfigSource& config,
                                absl::string_view type_url, Stats::Scope& scope,
                                SubscriptionCallbacks& callbacks,
                                OpaqueResourceDecoderSharedPtr resource_decoder) PURE;
};

} // namespace Config
} // namespace Envoy
