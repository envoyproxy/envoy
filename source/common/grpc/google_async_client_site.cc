// Site-specific customizations for Google gRPC client implementation.

#include "common/config/datasource.h"
#include "common/grpc/google_async_client_impl.h"

#include "grpc++/channel.h"
#include "grpc++/create_channel.h"
#include "grpc++/security/credentials.h"

namespace Envoy {
namespace Grpc {

std::shared_ptr<grpc::Channel>
GoogleAsyncClientImpl::createChannel(const envoy::api::v2::core::GrpcService::GoogleGrpc& config) {
  // TODO(htuch): add support for OAuth2, GCP, etc. credentials.
  std::shared_ptr<grpc::ChannelCredentials> creds;
  if (config.has_ssl_credentials()) {
    const grpc::SslCredentialsOptions ssl_creds = {
        .pem_root_certs = Config::DataSource::read(config.ssl_credentials().root_certs(), true),
        .pem_private_key = Config::DataSource::read(config.ssl_credentials().private_key(), true),
        .pem_cert_chain = Config::DataSource::read(config.ssl_credentials().cert_chain(), true),
    };
    creds = grpc::SslCredentials(ssl_creds);
  } else {
    creds = grpc::InsecureChannelCredentials();
  }
  return CreateChannel(config.target_uri(), creds);
}

} // namespace Grpc
} // namespace Envoy
