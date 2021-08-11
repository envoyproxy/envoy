#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/google_grpc_creds.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace Example {

/**
 * Access token implementation of Google Grpc Credentials Factory
 * This implementation uses ssl creds for the grpc channel if available, similar to the default
 * implementation. Additionally, it uses MetadataCredentialsFromPlugin to add a static secret to a
 * header for call credentials. This implementation does the same thing as AccessTokenCredentials,
 * but it's implemented as a Google gRPC client library plugin to show how a custom implementation
 * would be created.
 *
 * This implementation uses the access_token field in the config to get the secret to add to the
 * header.
 *
 * This can be used as an example for how to implement a more complicated custom call credentials
 * implementation. Any blocking calls should be performed in the
 * MetadataCredentialsFromPlugin::GetMetadata to ensure that the main thread is not blocked while
 * initializing the channel.
 */
class AccessTokenExampleGrpcCredentialsFactory : public Grpc::GoogleGrpcCredentialsFactory {
public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::config::core::v3::GrpcService& grpc_service_config,
                        Api::Api& api) override;

  std::string name() const override { return "envoy.grpc_credentials.access_token_example"; }
};

/*
 * Reference:
 * https://grpc.io/docs/guides/auth.html#extending-grpc-to-support-other-authentication-mechanisms
 */
class StaticHeaderAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
  StaticHeaderAuthenticator(const grpc::string& ticket) : ticket_(ticket) {}

  grpc::Status GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                           std::multimap<grpc::string, grpc::string>* metadata) override;

private:
  grpc::string ticket_;
};

} // namespace Example
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
