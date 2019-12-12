#pragma once

#include "envoy/api/api.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Config {

class SubscriptionFactoryImpl : public SubscriptionFactory {
public:
  SubscriptionFactoryImpl(const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                          Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
                          ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Config::SubscriptionFactory
  SubscriptionPtr subscriptionFromConfigSource(const envoy::api::v2::core::ConfigSource& config,
                                               absl::string_view type_url, Stats::Scope& scope,
                                               SubscriptionCallbacks& callbacks) override;

private:
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cm_;
  Runtime::RandomGenerator& random_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

} // namespace Config
} // namespace Envoy
