#pragma once

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/connection_balancer.h"

#include "source/common/network/connection_balancer_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Network {

class ExactConnectionBalancerFactory : public Envoy::Network::ConnectionBalanceFactory {
public:
  std::string name() const override { return "envoy.connection_balance.exact"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Envoy::Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::static_pointer_cast<Envoy::Network::ConnectionBalancer>(
        std::make_shared<Envoy::Network::ExactConnectionBalancerImpl>());
  }
};

class NopConnectionBalancerFactory : public Envoy::Network::ConnectionBalanceFactory {
public:
  std::string name() const override { return "envoy.connection_balance.nop"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::config::listener::v3::Listener::ConnectionBalanceConfig::NopBalance>();
  }

  Envoy::Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::static_pointer_cast<Envoy::Network::ConnectionBalancer>(
        std::make_shared<Envoy::Network::NopConnectionBalancerImpl>());
  }
};

REGISTER_FACTORY(NopConnectionBalancerFactory, Envoy::Network::ConnectionBalanceFactory);

} // namespace Network
} // namespace Common
} // namespace Extensions
} // namespace Envoy
