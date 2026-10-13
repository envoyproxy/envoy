#include "contrib/reverse_tunnel_reporter/source/clients/grpc_client/factory.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseTunnelReporterClientPtr
GrpcClientFactory::createClient(Server::Configuration::ServerFactoryContext& context,
                                const Protobuf::Message& config) {
  const auto& grpc_client_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::reverse_tunnel_reporters::v3alpha::
                                           clients::grpc_client::GrpcClientConfig&>(
          config, context.messageValidationVisitor());
  return std::make_unique<GrpcClient>(context, grpc_client_config);
}

std::string GrpcClientFactory::name() const {
  return "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.clients.grpc_client";
}

ProtobufTypes::MessagePtr GrpcClientFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::
                              grpc_client::GrpcClientConfig>();
}

REGISTER_FACTORY(GrpcClientFactory, ReverseTunnelReporterClientFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
