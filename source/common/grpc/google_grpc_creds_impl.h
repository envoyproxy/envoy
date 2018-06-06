#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

grpc::SslCredentialsOptions buildSslOptionsFromConfig(
    const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config);

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service);

std::shared_ptr<grpc::ChannelCredentials>
defaultSslChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                             bool allow_insecure = true);

std::shared_ptr<grpc::ChannelCredentials>
defaultChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                          bool allow_insecure = true);

} // namespace Grpc
} // namespace Envoy
