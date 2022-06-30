#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/connection_balancer_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "contrib/network/connection_balance/random/source/connection_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Random {

class RandomConnectionBalanceFactory : public Envoy::Network::ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Envoy::Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::make_shared<RandomConnectionBalancerImpl>();
  }

  std::string name() const override { return "envoy.network.connection_balance.random"; }
};

REGISTER_FACTORY(RandomConnectionBalanceFactory, Envoy::Network::ConnectionBalanceFactory);

} // namespace Random
} // namespace Extensions
} // namespace Envoy
