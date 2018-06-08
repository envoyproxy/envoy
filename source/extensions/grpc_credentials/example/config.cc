#include "extensions/grpc_credentials/example/config.h"

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/grpc/google_grpc_creds_impl.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace Example {

std::shared_ptr<grpc::ChannelCredentials>
AccessTokenExampleGrpcCredentialsFactory::getChannelCredentials(
    const envoy::api::v2::core::GrpcService& grpc_service_config) {
  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds =
      Grpc::CredsUtility::defaultSslChannelCredentials(grpc_service_config);
  std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
  for (const auto& credential : google_grpc.call_credentials()) {
    switch (credential.credential_specifier_case()) {
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kAccessToken: {
      if (!credential.access_token().empty()) {
        std::shared_ptr<grpc::CallCredentials> new_call_creds = grpc::MetadataCredentialsFromPlugin(
            std::make_unique<StaticHeaderAuthenticator>(credential.access_token()));
        if (call_creds == nullptr) {
          call_creds = new_call_creds;
        } else {
          call_creds = grpc::CompositeCallCredentials(call_creds, new_call_creds);
        }
      }
      break;
    }
    default:
      // unused credential types
      continue;
    }
  }
  if (call_creds != nullptr) {
    return grpc::CompositeChannelCredentials(creds, call_creds);
  }
  return creds;
}

grpc::Status
StaticHeaderAuthenticator::GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                                       std::multimap<grpc::string, grpc::string>* metadata) {
  // this function is run on a separate thread by the gRPC client library (independent of Envoy
  // threading), so it can perform actions such as refreshing an access token without blocking
  // the main thread. see:
  // https://grpc.io/grpc/cpp/classgrpc_1_1_metadata_credentials_plugin.html#a6faf44f7c08d0311a38a868fdb8cbaf0
  metadata->insert(std::make_pair("authorization", "Bearer " + ticket_));
  return grpc::Status::OK;
}

/**
 * Static registration for the static header Google gRPC credentials factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<AccessTokenExampleGrpcCredentialsFactory,
                                 Grpc::GoogleGrpcCredentialsFactory>
    access_token_google_grpc_credentials_registered_;

} // namespace Example
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
