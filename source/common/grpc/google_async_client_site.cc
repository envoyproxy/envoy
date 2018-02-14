// Site-specific customizations for Google gRPC client implementation.

#include "common/grpc/google_async_client_impl.h"

#include "grpc++/channel.h"
#include "grpc++/create_channel.h"
#include "grpc++/security/credentials.h"

namespace Envoy {
namespace Grpc {

std::shared_ptr<grpc::Channel>
GoogleAsyncClientImpl::createChannel(const envoy::api::v2::core::GrpcService::GoogleGrpc& config) {
  // TODO(htuch): add support for SSL, OAuth2, GCP, etc. credentials.
  std::shared_ptr<grpc::ChannelCredentials> creds = grpc::InsecureChannelCredentials();
  return CreateChannel(config.target_uri(), creds);
}

} // namespace Grpc
} // namespace Envoy
