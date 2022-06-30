#include "contrib/envoy/extensions/network/connection_balance/random/v3alpha/random.pb.h"
#include "contrib/network/connection_balance/random/source/connection_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Random {

ProtobufTypes::MessagePtr createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::network::connection_balance::random::v3alpha::Random>();
}

Envoy::Network::ConnectionBalancerSharedPtr
createConnectionBalancerFromProto(const Protobuf::Message&,
                                  Server::Configuration::FactoryContext& context) {

  return std::make_shared<RandomConnectionBalancerImpl>(context.api().randomGenerator());
}

} // namespace Random
} // namespace Extensions
} // namespace Envoy
