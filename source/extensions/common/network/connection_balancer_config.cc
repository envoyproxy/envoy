#include "source/extensions/common/network/connection_balancer_config.h"

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/connection_balancer.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Network {

ProtobufTypes::MessagePtr ExactConnectionBalancerFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::config::listener::v3::Listener::ConnectionBalanceConfig::ExactBalance>();
}

REGISTER_FACTORY(ExactConnectionBalancerFactory, Envoy::Network::ConnectionBalanceFactory);

} // namespace Network
} // namespace Common
} // namespace Extensions
} // namespace Envoy
