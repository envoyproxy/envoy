#pragma once

#include "envoy/config/grpc_credential/v2alpha/aws_iam.pb.h"
#include "envoy/grpc/google_grpc_creds.h"

#include "common/aws/signer.h"
#include "common/protobuf/protobuf.h"

#include "extensions/grpc_credentials/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace AwsIam {

class AwsIamGrpcCredentialsFactory : public Grpc::GoogleGrpcCredentialsFactory {
public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                        Grpc::GoogleGrpcCredentialsFactoryContext& context) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() {
    return std::make_unique<envoy::config::grpc_credential::v2alpha::AwsIamConfig>();
  }

  std::string name() const override { return GrpcCredentialsNames::get().AwsIam; }
};

class AwsIamAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
  AwsIamAuthenticator(Aws::Auth::SignerSharedPtr signer) : signer_(signer) {}

  grpc::Status GetMetadata(grpc::string_ref service_url, grpc::string_ref method_name,
                           const grpc::AuthContext&,
                           std::multimap<grpc::string, grpc::string>* metadata) override;

private:
  Aws::Auth::SignerSharedPtr signer_;
  const envoy::config::grpc_credential::v2alpha::AwsIamConfig config_;
};

} // namespace AwsIam
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
