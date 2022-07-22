#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/network/connection_balancer_impl.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace ConnectioBalacnce {
namespace Random {

class RandomConnectionBalanceFactory : public Envoy::Network::ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Envoy::Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override;

  std::string name() const override { return "envoy.network.connection_balance.random"; }
};

} // namespace Random
} // namespace ConnectioBalacnce
} // namespace Network
} // namespace Extensions
} // namespace Envoy
