#include "envoy/grpc/google_async_site.h"

namespace Envoy {
namespace Grpc {

std::shared_ptr<grpc::ChannelCredentials>
GoogleSite::channelCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& /*config*/) {
  return nullptr;
}

} // namespace Grpc
} // namespace Envoy
