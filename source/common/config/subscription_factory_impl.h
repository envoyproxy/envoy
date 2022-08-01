#pragma once

#include "envoy/api/api.h"
#include "envoy/common/key_value_store.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/config_updated_callback.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/server/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Config {

class SubscriptionFactoryImpl : public SubscriptionFactory, Logger::Loggable<Logger::Id::config> {
public:
  SubscriptionFactoryImpl(
      const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
      Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
      Api::Api& api, const Server::Instance& server, KeyValueStore* xds_store,
      ConfigUpdatedCallbackFactory* config_updated_callback_factory,
      const envoy::config::core::v3::TypedExtensionConfig* config_updated_callback_config);

  // Config::SubscriptionFactory
  SubscriptionPtr subscriptionFromConfigSource(const envoy::config::core::v3::ConfigSource& config,
                                               absl::string_view type_url, Stats::Scope& scope,
                                               SubscriptionCallbacks& callbacks,
                                               OpaqueResourceDecoder& resource_decoder,
                                               const SubscriptionOptions& options) override;
  SubscriptionPtr
  collectionSubscriptionFromUrl(const xds::core::v3::ResourceLocator& collection_locator,
                                const envoy::config::core::v3::ConfigSource& config,
                                absl::string_view resource_type, Stats::Scope& scope,
                                SubscriptionCallbacks& callbacks,
                                OpaqueResourceDecoder& resource_decoder) override;

private:
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cm_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
  const Server::Instance& server_;
  // Not owned; can be nullptr.
  KeyValueStore* xds_store_ = nullptr;
  // Not owned; can be nullptr.
  ConfigUpdatedCallbackFactory* config_updated_callback_factory_ = nullptr;
  // Not owned; can be nullptr. Will only be set if config_updated_callback_factory_  is not null.
  const envoy::config::core::v3::TypedExtensionConfig* config_updated_callback_config_ = nullptr;
};

} // namespace Config
} // namespace Envoy
