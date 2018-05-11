#include "common/grpc/google_grpc_creds_impl.h"

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/config/datasource.h"

namespace Envoy {
namespace Grpc {

/**
 * TODO: Create GoogleGrpcCredentialsFactory for each built-in credential type defined in Google
 * gRPC.
 */

grpc::SslCredentialsOptions buildSslOptionsFromConfig(
    const envoy::api::v2::core::GrpcService::GoogleGrpc::SslCredentials& ssl_config) {
  return {
      .pem_root_certs = Config::DataSource::read(ssl_config.root_certs(), true),
      .pem_private_key = Config::DataSource::read(ssl_config.private_key(), true),
      .pem_cert_chain = Config::DataSource::read(ssl_config.cert_chain(), true),
  };
}

/**
 * Default implementation of Google Grpc Credentials Factory
 * Uses ssl creds if available, or defaults to insecure channel.
 *
 * This is not the same as google_default credentials. This is the default implementation that is
 * loaded if no other implementation is configured.
 */
class DefaultGoogleGrpcCredentialsFactory : public GoogleGrpcCredentialsFactory {

public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) override {
    const auto& google_grpc = grpc_service_config.google_grpc();
    std::shared_ptr<grpc::ChannelCredentials> creds = grpc::InsecureChannelCredentials();
    if (google_grpc.has_channel_credentials() &&
        google_grpc.channel_credentials().has_ssl_credentials()) {
      return grpc::SslCredentials(
          buildSslOptionsFromConfig(google_grpc.channel_credentials().ssl_credentials()));
    }
    return creds;
  }

  std::string name() const override { return "envoy.grpc_credentials.default"; }
};

/**
 * Static registration for the default Google gRPC credentials factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<DefaultGoogleGrpcCredentialsFactory, GoogleGrpcCredentialsFactory>
    default_google_grpc_credentials_registered_;

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service) {
  GoogleGrpcCredentialsFactory* credentials_factory = nullptr;
  const std::string& google_grpc_credentials_factory_name =
      grpc_service.google_grpc().credentials_factory_name();
  if (google_grpc_credentials_factory_name.empty()) {
    credentials_factory = Registry::FactoryRegistry<GoogleGrpcCredentialsFactory>::getFactory(
        "envoy.grpc_credentials.default");
  } else {
    credentials_factory = Registry::FactoryRegistry<GoogleGrpcCredentialsFactory>::getFactory(
        google_grpc_credentials_factory_name);
  }
  if (credentials_factory == nullptr) {
    throw EnvoyException(fmt::format("Unknown google grpc credentials factory: {}",
                                     google_grpc_credentials_factory_name));
  }
  return credentials_factory->getChannelCredentials(grpc_service);
}

} // namespace Grpc
} // namespace Envoy
