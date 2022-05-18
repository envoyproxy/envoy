#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class DummyConnectionBalanceFactory : public ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override;

  std::string name() const override { return "envoy.network.connection_balance.dummy"; }
};

ProtobufTypes::MessagePtr DummyConnectionBalanceFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
}

ConnectionBalancerSharedPtr DummyConnectionBalanceFactory::createConnectionBalancerFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext&) {
  return std::make_shared<MockConnectionBalancer>();
}

TEST(ConnectionBalanceFactory, TestCreateConnectionBalancer) {
  DummyConnectionBalanceFactory factory;
  Registry::InjectFactory<ConnectionBalanceFactory> registration(factory);

  auto dummy_factory = Registry::FactoryRegistry<ConnectionBalanceFactory>::getFactory(
      "envoy.network.connection_balance.dummy");
  ASSERT_NE(dummy_factory, nullptr);

  ProtobufTypes::MessagePtr message = dummy_factory->createEmptyConfigProto();
  ASSERT_NE(message, nullptr);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ConnectionBalancerSharedPtr connection_balancer =
      dummy_factory->createConnectionBalancerFromProto(*message, factory_context);
  ASSERT_NE(connection_balancer.get(), nullptr);
}

} // namespace
} // namespace Network
} // namespace Envoy
