#pragma once

#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Upstream {

class LoadBalancerConfigWrapper : public LoadBalancerConfig {
public:
  LoadBalancerConfigWrapper(ProtobufTypes::MessagePtr config) : config_(std::move(config)) {}

  template <typename ProtoType> OptRef<const ProtoType> typedProtoConfig() const {
    auto* typed_config = dynamic_cast<const ProtoType*>(config_.get());
    return makeOptRefFromPtr<const ProtoType>(typed_config);
  }

private:
  ProtobufTypes::MessagePtr config_;
};

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

  LoadBalancerConfigPtr loadConfig(ProtobufTypes::MessagePtr config,
                                   ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<LoadBalancerConfigWrapper>(std::move(config));
  }

protected:
  TypedLoadBalancerFactoryBase(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace Upstream
} // namespace Envoy
