#include "extensions/grpc_credentials/file_based_metadata/config.h"

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/config/grpc_credentials/v2alpha/file_based_metadata.pb.validate.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/config/datasource.h"
#include "common/config/utility.h"
#include "common/grpc/google_grpc_creds_impl.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace FileBasedMetadata {

std::shared_ptr<grpc::ChannelCredentials>
FileBasedMetadataGrpcCredentialsFactory::getChannelCredentials(
    const envoy::api::v2::core::GrpcService& grpc_service_config) {
  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds =
      defaultSslChannelCredentials(grpc_service_config, false);
  std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
  for (const auto& credential : google_grpc.call_credentials()) {
    switch (credential.credential_specifier_case()) {
    case envoy::api::v2::core::GrpcService::GoogleGrpc::CallCredentials::kFromPlugin: {
      if (credential.from_plugin().name() == GrpcCredentialsNames::get().FILE_BASED_METADATA) {
        FileBasedMetadataGrpcCredentialsFactory file_based_metadata_credentials_factory;
        const Envoy::ProtobufTypes::MessagePtr file_based_metadata_config_message =
            Envoy::Config::Utility::translateToFactoryConfig(
                credential.from_plugin(), file_based_metadata_credentials_factory);
        const auto& file_based_metadata_config = Envoy::MessageUtil::downcastAndValidate<
            const envoy::config::grpc_credentials::v2alpha::FileBasedMetadataConfig&>(
            *file_based_metadata_config_message);
        std::shared_ptr<grpc::CallCredentials> new_call_creds = grpc::MetadataCredentialsFromPlugin(
            std::make_unique<FileBasedMetadataAuthenticator>(file_based_metadata_config));
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
FileBasedMetadataAuthenticator::GetMetadata(grpc::string_ref, grpc::string_ref,
                                            const grpc::AuthContext&,
                                            std::multimap<grpc::string, grpc::string>* metadata) {
  std::string header_value = Envoy::Config::DataSource::read(config_.secret_data(), true);
  std::string header_key = "authorization";
  std::string header_prefix = config_.header_prefix();
  if (!config_.header_key().empty()) {
    header_key = config_.header_key();
  }
  metadata->insert(std::make_pair(header_key, header_prefix + header_value));
  return grpc::Status::OK;
}

/**
 * Static registration for the file based metadata Google gRPC credentials factory. @see
 * RegisterFactory.
 */
static Registry::RegisterFactory<FileBasedMetadataGrpcCredentialsFactory,
                                 Grpc::GoogleGrpcCredentialsFactory>
    file_based_metadata_google_grpc_credentials_registered_;

} // namespace FileBasedMetadata
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
