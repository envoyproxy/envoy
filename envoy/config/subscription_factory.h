#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/local_info/local_info.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/scope.h"

#include "xds/core/v3/resource_locator.pb.h"

namespace Envoy {
namespace Config {

class SubscriptionFactory {
public:
  virtual ~SubscriptionFactory() = default;

  static constexpr uint32_t RetryInitialDelayMs = 500;
  static constexpr uint32_t RetryMaxDelayMs = 30000; // Do not cross more than 30s

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

// A factory class that individual config subscriptions can subclass to be factory-created.
// TODO(alyssawilk) rename once https://github.com/envoyproxy/envoy/pull/26468 lands
class ConfigSubscriptionFactory : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.config_subscription"; }
  virtual SubscriptionPtr create(const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Api::Api& api, const envoy::config::core::v3::ConfigSource& config,
                                 absl::string_view type_url, SubscriptionCallbacks& callbacks,
                                 OpaqueResourceDecoderSharedPtr resource_decoder,
                                 SubscriptionStats stats,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace Config
} // namespace Envoy
