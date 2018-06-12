#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

grpc::SslCredentialsOptions buildSslOptionsFromConfig(
    const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config);

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service);

class CredsUtility {
public:
  /**
   * Translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials
   * for SSL channel credentials.
   * @param google_grpc Google gRPC config.
   * @return std::shared_ptr<grpc::ChannelCredentials> SSL channel credentials. A nullptr
   *         will be returned in the absence of any configured SSL in grpc_service_config.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  sslChannelCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& google_grpc);

  /**
   * Static translation from envoy::api::v2::core::GrpcService to a vector of grpc::CallCredentials.
   * Any plugin based call credentials will be elided.
   * @param grpc_service Google gRPC config.
   * @return std::vector<std::shared_ptr<grpc::CallCredentials>> call credentials.
   */
  static std::vector<std::shared_ptr<grpc::CallCredentials>>
  callCredentials(const envoy::api::v2::core::GrpcService::GoogleGrpc& google_grpc);

  /**
   * Default translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials for SSL
   * channel credentials.
   * @param grpc_service_config gRPC service config.
   * @return std::shared_ptr<grpc::ChannelCredentials> SSL channel credentials. Empty SSL
   *         credentials will be set in the absence of any configured SSL in grpc_service_config,
   *         forcing the channel to SSL.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  defaultSslChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config);

  /**
   * Default static translation from envoy::api::v2::core::GrpcService to grpc::ChannelCredentials
   * for all non-plugin based channel and call credentials.
   * @param grpc_service_config gRPC service config.
   * @return std::shared_ptr<grpc::ChannelCredentials> composite channel and call credentials.
   *         will be set in the absence of any configured SSL in grpc_service_config, forcing the
   *         channel to SSL.
   */
  static std::shared_ptr<grpc::ChannelCredentials>
  defaultChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config);
};

} // namespace Grpc
} // namespace Envoy
