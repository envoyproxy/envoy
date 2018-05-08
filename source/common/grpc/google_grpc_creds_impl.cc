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

/**
 * Default implementation of Google Grpc Credentials Factory
 * Uses ssl creds if available, or defaults to insecure channel.
 */
class DefaultGoogleGrpcCredentialsFactory : public GoogleGrpcCredentialsFactory {

public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) override {
    const auto& google_grpc = grpc_service_config.google_grpc();
    std::shared_ptr<grpc::ChannelCredentials> creds = grpc::InsecureChannelCredentials();
    for (const auto& credential : google_grpc.credentials()) {
      if (credential.has_channel_credentials() &&
          credential.channel_credentials().has_ssl_credentials()) {
        const grpc::SslCredentialsOptions ssl_creds = {
            .pem_root_certs = Config::DataSource::read(
                credential.channel_credentials().ssl_credentials().root_certs(), true),
            .pem_private_key = Config::DataSource::read(
                credential.channel_credentials().ssl_credentials().private_key(), true),
            .pem_cert_chain = Config::DataSource::read(
                credential.channel_credentials().ssl_credentials().cert_chain(), true),
        };
        return grpc::SslCredentials(ssl_creds);
      }
    }
    return creds;
  }

  std::string name() const override { return "envoy.grpc_credentials.default"; }
};

/**
 * Access token implementation of Google Grpc Credentials Factory
 * This implementation uses ssl creds for the grpc channel if available, similar to the default
 * implementation. Additionally, it uses MetadataCredentialsFromPlugin to add a static secret to a
 * header for call credentials. This implementation does the same thing as AccessTokenCredentials,
 * but it's implemented as a plugin to show how a custom implementation would be created.
 *
 * This implementation uses the access_token field in the config to get the secret to add to the
 * header.
 *
 * This can be used as an example for how to implement a more complicated custom call credentials
 * implementation. Any blocking calls should be performed in the
 * MetadataCredentialsFromPlugin::GetMetadata to ensure that the main thread is not blocked while
 * initializing the channel.
 */
class AccessTokenGrpcCredentialsFactory : public GoogleGrpcCredentialsFactory {

public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) override {
    const auto& google_grpc = grpc_service_config.google_grpc();
    std::shared_ptr<grpc::ChannelCredentials> creds =
        grpc::SslCredentials(grpc::SslCredentialsOptions());
    std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
    for (const auto& credential : google_grpc.credentials()) {
      switch (credential.credential_specifier_case()) {
      case envoy::api::v2::core::GrpcService::GoogleGrpc::Credentials::kChannelCredentials: {
        if (credential.channel_credentials().has_ssl_credentials()) {
          const grpc::SslCredentialsOptions ssl_creds = {
              .pem_root_certs = Config::DataSource::read(
                  credential.channel_credentials().ssl_credentials().root_certs(), true),
              .pem_private_key = Config::DataSource::read(
                  credential.channel_credentials().ssl_credentials().private_key(), true),
              .pem_cert_chain = Config::DataSource::read(
                  credential.channel_credentials().ssl_credentials().cert_chain(), true),
          };
          creds = grpc::SslCredentials(ssl_creds);
        }
        break;
      }
      case envoy::api::v2::core::GrpcService::GoogleGrpc::Credentials::kCallCredentials: {
        if (!credential.call_credentials().access_token().empty()) {
          call_creds =
              grpc::MetadataCredentialsFromPlugin(std::unique_ptr<grpc::MetadataCredentialsPlugin>(
                  new StaticHeaderAuthenticator(credential.call_credentials().access_token())));
        }
        break;
      }
      default:
        // Validated by schema.
        NOT_REACHED;
      }
    }
    if (call_creds != nullptr) {
      return grpc::CompositeChannelCredentials(creds, call_creds);
    }
    return creds;
  }

  std::string name() const override { return "envoy.grpc_credentials.access_token"; }

private:
  /*
   * Reference:
   * https://grpc.io/docs/guides/auth.html#extending-grpc-to-support-other-authentication-mechanisms
   */
  class StaticHeaderAuthenticator : public grpc::MetadataCredentialsPlugin {
  public:
    StaticHeaderAuthenticator(const grpc::string& ticket) : ticket_(ticket) {}

    grpc::Status GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                             std::multimap<grpc::string, grpc::string>* metadata) override {
      // this function is run on a separate thread by the gRPC client library (independent of Envoy
      // threading), so it can perform actions such as refreshing an access token without blocking
      // the main thread. see:
      // https://grpc.io/grpc/cpp/classgrpc_1_1_metadata_credentials_plugin.html#a6faf44f7c08d0311a38a868fdb8cbaf0
      metadata->insert(std::make_pair("authorization", "Bearer " + ticket_));
      return grpc::Status::OK;
    }

  private:
    grpc::string ticket_;
  };
};

/**
 * Static registration for the default Google gRPC credentials factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<DefaultGoogleGrpcCredentialsFactory, GoogleGrpcCredentialsFactory>
    default_google_grpc_credentials_registered_;

/**
 * Static registration for the static header Google gRPC credentials factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<AccessTokenGrpcCredentialsFactory, GoogleGrpcCredentialsFactory>
    access_token_google_grpc_credentials_registered_;

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
