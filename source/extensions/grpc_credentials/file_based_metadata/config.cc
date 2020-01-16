#include "extensions/grpc_credentials/file_based_metadata/config.h"

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/file_based_metadata.pb.h"
#include "envoy/config/grpc_credential/v3/file_based_metadata.pb.validate.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/config/datasource.h"
#include "common/config/utility.h"
#include "common/grpc/google_grpc_creds_impl.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace FileBasedMetadata {

std::shared_ptr<grpc::ChannelCredentials>
FileBasedMetadataGrpcCredentialsFactory::getChannelCredentials(
    const envoy::config::core::v3::GrpcService& grpc_service_config, Api::Api& api) {
  const auto& google_grpc = grpc_service_config.google_grpc();
  std::shared_ptr<grpc::ChannelCredentials> creds =
      Grpc::CredsUtility::defaultSslChannelCredentials(grpc_service_config, api);
  std::shared_ptr<grpc::CallCredentials> call_creds = nullptr;
  for (const auto& credential : google_grpc.call_credentials()) {
    switch (credential.credential_specifier_case()) {
    case envoy::config::core::v3::GrpcService::GoogleGrpc::CallCredentials::
        CredentialSpecifierCase::kFromPlugin: {
      if (credential.from_plugin().name() == GrpcCredentialsNames::get().FileBasedMetadata) {
        FileBasedMetadataGrpcCredentialsFactory file_based_metadata_credentials_factory;
        // We don't deal with validation failures here at runtime today, see
        // https://github.com/envoyproxy/envoy/issues/8010.
        const Envoy::ProtobufTypes::MessagePtr file_based_metadata_config_message =
            Envoy::Config::Utility::translateToFactoryConfig(
                credential.from_plugin(), ProtobufMessage::getNullValidationVisitor(),
                file_based_metadata_credentials_factory);
        const auto& file_based_metadata_config = Envoy::MessageUtil::downcastAndValidate<
            const envoy::config::grpc_credential::v3::FileBasedMetadataConfig&>(
            *file_based_metadata_config_message, ProtobufMessage::getNullValidationVisitor());
        std::shared_ptr<grpc::CallCredentials> new_call_creds = grpc::MetadataCredentialsFromPlugin(
            std::make_unique<FileBasedMetadataAuthenticator>(file_based_metadata_config, api));
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
  std::string header_key = "authorization";
  std::string header_prefix = config_.header_prefix();
  if (!config_.header_key().empty()) {
    header_key = config_.header_key();
  }
  try {
    std::string header_value = Envoy::Config::DataSource::read(config_.secret_data(), true, api_);
    metadata->insert(std::make_pair(header_key, header_prefix + header_value));
  } catch (const EnvoyException& e) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, e.what());
  }
  return grpc::Status::OK;
}

/**
 * Static registration for the file based metadata Google gRPC credentials factory. @see
 * RegisterFactory.
 */
REGISTER_FACTORY(FileBasedMetadataGrpcCredentialsFactory, Grpc::GoogleGrpcCredentialsFactory);

} // namespace FileBasedMetadata
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
