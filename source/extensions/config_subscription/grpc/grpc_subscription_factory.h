#pragma once

#include "envoy/config/subscription_factory.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Config {

class GrpcConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.grpc"; }
  SubscriptionPtr create(SubscriptionData& data) override;
};

class DeltaGrpcConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.delta_grpc"; }
  SubscriptionPtr create(SubscriptionData& data) override;
};

class AdsConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.ads"; }
  SubscriptionPtr create(SubscriptionData& data) override;
};

DECLARE_FACTORY(GrpcConfigSubscriptionFactory);
DECLARE_FACTORY(DeltaGrpcConfigSubscriptionFactory);
DECLARE_FACTORY(AdsConfigSubscriptionFactory);

} // namespace Config
} // namespace Envoy
