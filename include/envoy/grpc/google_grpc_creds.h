#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/common/pure.h"

#include "common/config/datasource.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

/**
 * Interface for all Google gRPC credentials factories.
 */
class GoogleGrpcCredentialsFactory {
public:
  virtual ~GoogleGrpcCredentialsFactory() {}

  /**
   * Get a ChannelCredentials to be used for authentication of a gRPC channel.
   *
   * GoogleGrpcCredentialsFactory should always return a ChannelCredentials. To use CallCredentials,
   * the ChannelCredentials can be created by using a combination of CompositeChannelCredentials and
   * CompositeCallCredentials to combine multiple credentials.
   *
   * @param grpc_service_config contains configuration options
   * @return std::shared_ptr<grpc::ChannelCredentials> to be used to authenticate a Google gRPC
   * channel.
   */
  virtual std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of
   * a Google gRPC credentials factory.
   */
  virtual std::string name() const PURE;

protected:
  grpc::SslCredentialsOptions buildSslOptionsFromConfig(
      const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config) {
    return {
        .pem_root_certs = Config::DataSource::read(ssl_config.root_certs(), true),
        .pem_private_key = Config::DataSource::read(ssl_config.private_key(), true),
        .pem_cert_chain = Config::DataSource::read(ssl_config.cert_chain(), true),
    };
  }

  std::shared_ptr<grpc::ChannelCredentials>
  defaultSslChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                               bool allow_insecure = true) {
    const auto& google_grpc = grpc_service_config.google_grpc();
    std::shared_ptr<grpc::ChannelCredentials> creds = nullptr;
    if (allow_insecure) {
      creds = grpc::InsecureChannelCredentials();
    } else {
      creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
    }
    if (google_grpc.has_channel_credentials() &&
        google_grpc.channel_credentials().has_ssl_credentials()) {
      return grpc::SslCredentials(
          buildSslOptionsFromConfig(google_grpc.channel_credentials().ssl_credentials()));
    }
    return creds;
  }
};

} // namespace Grpc
} // namespace Envoy
