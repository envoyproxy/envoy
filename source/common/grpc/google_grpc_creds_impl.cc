#include "common/grpc/google_grpc_creds_impl.h"

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/config/datasource.h"

namespace Envoy {
namespace Grpc {

std::shared_ptr<grpc::ChannelCredentials>
defaultSslChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                             bool allow_insecure) {
  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds = nullptr;
  if (allow_insecure) {
    creds = grpc::InsecureChannelCredentials();
  } else {
    creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
  }
  if (google_grpc.has_channel_credentials() &&
      google_grpc.channel_credentials().has_ssl_credentials()) {
    const auto& ssl_credentials = google_grpc.channel_credentials().ssl_credentials();
    const grpc::SslCredentialsOptions ssl_credentials_options = {
        .pem_root_certs = Config::DataSource::read(ssl_credentials.root_certs(), true),
        .pem_private_key = Config::DataSource::read(ssl_credentials.private_key(), true),
        .pem_cert_chain = Config::DataSource::read(ssl_credentials.cert_chain(), true),
    };
    return grpc::SslCredentials(ssl_credentials_options);
  }
  return creds;
}

std::shared_ptr<grpc::ChannelCredentials>
defaultChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                          bool allow_insecure) {
  std::shared_ptr<grpc::ChannelCredentials> creds =
      defaultSslChannelCredentials(grpc_service_config, allow_insecure);
  std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
  for (const auto& credential : grpc_service_config.google_grpc().call_credentials()) {
    std::shared_ptr<grpc::CallCredentials> new_call_creds;
    switch (credential.credential_specifier_case()) {
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kAccessToken: {
      new_call_creds = grpc::AccessTokenCredentials(credential.access_token());
      break;
    }
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kGoogleComputeEngine: {
      new_call_creds = grpc::GoogleComputeEngineCredentials();
      break;
    }
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kGoogleRefreshToken: {
      new_call_creds = grpc::GoogleRefreshTokenCredentials(credential.google_refresh_token());
      break;
    }
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kServiceAccountJwtAccess: {
      new_call_creds = grpc::ServiceAccountJWTAccessCredentials(
          credential.service_account_jwt_access().json_key(),
          credential.service_account_jwt_access().token_lifetime_seconds());
      break;
    }
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kGoogleIam: {
      new_call_creds = grpc::GoogleIAMCredentials(credential.google_iam().authorization_token(),
                                                  credential.google_iam().authority_selector());
      break;
    }
    default:
      // We don't handle plugin credentials here, callers can do so instead if they want.
      continue;
    }
    if (new_call_creds != nullptr) {
      if (call_creds == nullptr) {
        call_creds = new_call_creds;
      } else {
        call_creds = grpc::CompositeCallCredentials(call_creds, new_call_creds);
      }
    }
  }
  if (call_creds != nullptr) {
    return grpc::CompositeChannelCredentials(creds, call_creds);
  }
  return creds;
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
    return defaultChannelCredentials(grpc_service_config);
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
