#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Config {

class SubscriptionFactoryImpl : public SubscriptionFactory, Logger::Loggable<Logger::Id::config> {
public:
  SubscriptionFactoryImpl(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                          Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
                          ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                          Runtime::Loader& runtime);

  // Config::SubscriptionFactory
  SubscriptionPtr subscriptionFromConfigSource(const envoy::config::core::v3::ConfigSource& config,
                                               absl::string_view type_url, Stats::Scope& scope,
                                               SubscriptionCallbacks& callbacks,
                                               OpaqueResourceDecoder& resource_decoder) override;

private:
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cm_;
  Runtime::RandomGenerator& random_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
  Runtime::Loader& runtime_;
};

} // namespace Config
} // namespace Envoy
