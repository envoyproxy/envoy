#pragma once

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/grpc_client.pb.h"
#include "contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client/grpc_client.pb.validate.h"
#include "contrib/reverse_tunnel_reporter/source/clients/grpc_client/client.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class GrpcClientFactory : public ReverseTunnelReporterClientFactory {
public:
  ReverseTunnelReporterClientPtr createClient(Server::Configuration::ServerFactoryContext& context,
                                              const Protobuf::Message& config) override;

  std::string name() const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
