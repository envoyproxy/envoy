#pragma once

#include "envoy/api/api.h"
#include "envoy/common/backoff_strategy.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/custom_config_validators.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/local_info/local_info.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stats/scope.h"

#include "xds/core/v3/resource_locator.pb.h"

namespace Envoy {

namespace Server {
class Instance;
} // namespace Server
namespace Grpc {
class RawAsyncClient;
} // namespace Grpc

namespace Config {
class XdsResourcesDelegate;
class XdsConfigTracker;

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
class ConfigSubscriptionFactory : public Config::UntypedFactory {
public:
  struct SubscriptionData {
    const LocalInfo::LocalInfo& local_info_;
    Event::Dispatcher& dispatcher_;
    Upstream::ClusterManager& cm_;
    ProtobufMessage::ValidationVisitor& validation_visitor_;
    Api::Api& api_;
    const Server::Instance& server_;
    OptRef<XdsResourcesDelegate> xds_resources_delegate_;
    OptRef<XdsConfigTracker> xds_config_tracker_;

    const envoy::config::core::v3::ConfigSource& config_;
    absl::string_view type_url_;
    Stats::Scope& scope_;
    SubscriptionCallbacks& callbacks_;
    OpaqueResourceDecoderSharedPtr resource_decoder_;
    const SubscriptionOptions& options_;
    OptRef<const xds::core::v3::ResourceLocator> collection_locator_;
    SubscriptionStats stats_;
  };

  std::string category() const override { return "envoy.config_subscription"; }
  virtual SubscriptionPtr create(SubscriptionData& data) PURE;
};

class MuxFactory : public Config::UntypedFactory {
public:
  std::string category() const override { return "envoy.config_mux"; }
  virtual void shutdownAll() PURE;
  virtual std::shared_ptr<GrpcMux>
  create(std::unique_ptr<Grpc::RawAsyncClient>&& async_client, Event::Dispatcher& dispatcher,
         Random::RandomGenerator& random, Stats::Scope& scope,
         const envoy::config::core::v3::ApiConfigSource& ads_config,
         const LocalInfo::LocalInfo& local_info,
         std::unique_ptr<CustomConfigValidators>&& config_validators,
         BackOffStrategyPtr&& backoff_strategy, OptRef<XdsConfigTracker> xds_config_tracker,
         OptRef<XdsResourcesDelegate> xds_resources_delegate, bool use_eds_resources_cache) PURE;
};

} // namespace Config
} // namespace Envoy
