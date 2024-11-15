#pragma once

#include "envoy/config/subscription_factory.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Config {

class DeltaGrpcCollectionConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.delta_grpc_collection"; }
  SubscriptionPtr create(SubscriptionData& data) override;
};

class AggregatedGrpcCollectionConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override {
    return "envoy.config_subscription.aggregated_grpc_collection";
  }
  SubscriptionPtr create(SubscriptionData& data) override;
};

class AdsCollectionConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.ads_collection"; }
  SubscriptionPtr create(SubscriptionData& data) override;
};

DECLARE_FACTORY(DeltaGrpcCollectionConfigSubscriptionFactory);
DECLARE_FACTORY(AggregatedGrpcCollectionConfigSubscriptionFactory);
DECLARE_FACTORY(AdsCollectionConfigSubscriptionFactory);

} // namespace Config
} // namespace Envoy
