#pragma once

#include "envoy/config/grpc_credential/v2alpha/file_based_metadata.pb.h"
#include "envoy/grpc/google_grpc_creds.h"

#include "common/protobuf/protobuf.h"

#include "extensions/grpc_credentials/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace FileBasedMetadata {

/**
 * File Based Metadata implementation of Google Grpc Credentials Factory
 * This implementation uses ssl creds for the grpc channel if available. Additionally, it uses
 * MetadataCredentialsFromPlugin to add a static secret that is loaded from a file. The header key
 * and header prefix are configurable.
 *
 * This implementation uses the from_plugin field in the call credentials config to get the filename
 * of where the secret is stored to add to the header.
 */
class FileBasedMetadataGrpcCredentialsFactory : public Grpc::GoogleGrpcCredentialsFactory {
public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() {
    return std::make_unique<envoy::config::grpc_credential::v2alpha::FileBasedMetadataConfig>();
  }

  std::string name() const override { return GrpcCredentialsNames::get().FileBasedMetadata; }
};

class FileBasedMetadataAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
  FileBasedMetadataAuthenticator(
      const envoy::config::grpc_credential::v2alpha::FileBasedMetadataConfig config)
      : config_(config) {}

  grpc::Status GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                           std::multimap<grpc::string, grpc::string>* metadata) override;

private:
  const envoy::config::grpc_credential::v2alpha::FileBasedMetadataConfig config_;
};

} // namespace FileBasedMetadata
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
