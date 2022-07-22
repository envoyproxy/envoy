#include "contrib/network/connection_balance/random/source/config.h"

#include "envoy/registry/registry.h"

#include "contrib/envoy/extensions/network/connection_balance/random/v3alpha/random.pb.h"
#include "contrib/network/connection_balance/random/source/connection_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace ConnectioBalacnce {
namespace Random {

ProtobufTypes::MessagePtr RandomConnectionBalanceFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::network::connection_balance::random::v3alpha::Random>();
}

Envoy::Network::ConnectionBalancerSharedPtr
RandomConnectionBalanceFactory::createConnectionBalancerFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext& context) {

  return std::make_shared<RandomConnectionBalancerImpl>(context.api().randomGenerator());
}
REGISTER_FACTORY(RandomConnectionBalanceFactory, Envoy::Network::ConnectionBalanceFactory);

} // namespace Random
} // namespace ConnectioBalacnce
} // namespace Network
} // namespace Extensions
} // namespace Envoy
