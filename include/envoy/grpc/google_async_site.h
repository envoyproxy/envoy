#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "grpc++/grpc++.h"

namespace Envoy {
namespace Grpc {

// Google gRPC C++ library specific site specializations.
class GoogleSite {
public:
  /**
   * Extract site-specific channel credentials from Google gRPC service config.
   * @param config Google gRPC library service config.
   * @return std::shared_ptr<grpc::ChannelCredentials> channel credentials or nullptr if there are
   *         no site specific channel creds.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  channelCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& config);
};

} // namespace Grpc
} // namespace Envoy
