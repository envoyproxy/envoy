#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "grpc++/grpc++.h"

namespace Envoy {
namespace Grpc {

grpc::SslCredentialsOptions buildSslOptionsFromConfig(
    const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config);

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service);

} // namespace Grpc
} // namespace Envoy
