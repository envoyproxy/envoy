#pragma once

#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Upstream {

/**
 * Base class for cluster provided load balancers and load balancers specified by load balancing
 * policy config. This class should be extended directly if the load balancing policy specifies a
 * thread-aware load balancer.
 *
 * TODO: provide a ThreadLocalLoadBalancer construct to abstract away thread-awareness from load
 * balancing extensions that don't require it.
 */
template <class Proto> class TypedLoadBalancerFactoryBase : public TypedLoadBalancerFactory {
public:
  // Upstream::TypedLoadBalancerFactory
  std::string name() const override { return name_; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Proto()};
  }

protected:
  TypedLoadBalancerFactoryBase(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace Upstream
} // namespace Envoy
