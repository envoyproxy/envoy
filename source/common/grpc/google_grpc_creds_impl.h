#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "grpc++/grpc++.h"

namespace Envoy {
namespace Grpc {

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service);

} // namespace Grpc
} // namespace Envoy
