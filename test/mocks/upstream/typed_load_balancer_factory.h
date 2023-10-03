#pragma once

#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockTypedLoadBalancerFactory : public TypedLoadBalancerFactory {
public:
  class EmptyLoadBalancerConfig : public LoadBalancerConfig {
  public:
    EmptyLoadBalancerConfig() = default;
  };

  MockTypedLoadBalancerFactory();
  ~MockTypedLoadBalancerFactory() override;

  // Upstream::TypedLoadBalancerFactory
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(ThreadAwareLoadBalancerPtr, create,
              (OptRef<const LoadBalancerConfig> lb_config, const ClusterInfo& cluster_info,
               const PrioritySet& priority_set, Runtime::Loader& runtime,
               Random::RandomGenerator& random, TimeSource& time_source));

  LoadBalancerConfigPtr loadConfig(const Protobuf::Message&,
                                   ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<EmptyLoadBalancerConfig>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
};
} // namespace Upstream
} // namespace Envoy
