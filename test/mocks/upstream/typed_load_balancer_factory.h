#pragma once

#include "envoy/upstream/load_balancer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockTypedLoadBalancerFactory : public TypedLoadBalancerFactory {
public:
  MockTypedLoadBalancerFactory();
  ~MockTypedLoadBalancerFactory() override;

  // Upstream::TypedLoadBalancerFactory
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(ThreadAwareLoadBalancerPtr, create,
              (const PrioritySet& priority_set, ClusterStats& stats, Stats::Scope& stats_scope,
               Runtime::Loader& runtime, Random::RandomGenerator& random,
               const ::envoy::config::cluster::v3::LoadBalancingPolicy_Policy& lb_policy));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
};
} // namespace Upstream
} // namespace Envoy
