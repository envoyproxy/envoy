#include "server/config/network/ssl_socket.h"

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/auth/cert.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/network/raw_buffer_socket.h"
#include "common/protobuf/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/ssl_socket.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Network::TransportSocketFactoryPtr
UpstreamSslSocketFactory::createTransportSocketFactory(const Protobuf::Message& message,
                                                       TransportSocketFactoryContext& context) {
  return std::make_unique<Ssl::ClientSslSocketFactory>(
      Ssl::ClientContextConfigImpl(
          MessageUtil::downcastAndValidate<const envoy::api::v2::auth::UpstreamTlsContext&>(
              message)),
      context.sslContextManager(), context.statsScope());
}

ProtobufTypes::MessagePtr UpstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::auth::UpstreamTlsContext>();
}

static Registry::RegisterFactory<UpstreamSslSocketFactory, UpstreamTransportSocketConfigFactory>
    upstream_registered_;

Network::TransportSocketFactoryPtr DownstreamSslSocketFactory::createTransportSocketFactory(
    const std::string& listener_name, const std::vector<std::string>& server_names,
    bool skip_context_update, const Protobuf::Message& message,
    TransportSocketFactoryContext& context) {
  return std::make_unique<Ssl::ServerSslSocketFactory>(
      Ssl::ServerContextConfigImpl(
          MessageUtil::downcastAndValidate<const envoy::api::v2::auth::DownstreamTlsContext&>(
              message)),
      listener_name, server_names, skip_context_update, context.sslContextManager(),
      context.statsScope());
}

ProtobufTypes::MessagePtr DownstreamSslSocketFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::api::v2::auth::DownstreamTlsContext>();
}

static Registry::RegisterFactory<DownstreamSslSocketFactory, DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
