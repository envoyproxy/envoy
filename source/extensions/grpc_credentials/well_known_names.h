#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {

/**
 * Well-known gRPC Credentials names.
 * NOTE: New gRPC Credentials should use the well known name: envoy.grpc_credentials.name.
 */
class GrpcCredentialsNameValues {
public:
  // Access Token Example.
  const std::string AccessTokenExample = "envoy.grpc_credentials.access_token_example";
  // File Based Metadata credentials
  const std::string FileBasedMetadata = "envoy.grpc_credentials.file_based_metadata";
};

typedef ConstSingleton<GrpcCredentialsNameValues> GrpcCredentialsNames;

} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
