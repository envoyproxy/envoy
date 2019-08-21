#pragma once

#include "envoy/config/grpc_credential/v2alpha/aws_iam.pb.validate.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/http/header_map.h"

#include "common/http/message_impl.h"

#include "extensions/filters/http/common/aws/signer.h"
#include "extensions/grpc_credentials/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace GrpcCredentials {
namespace AwsIam {

/**
 * AWS IAM based gRPC channel credentials factory.
 */
class AwsIamGrpcCredentialsFactory : public Grpc::GoogleGrpcCredentialsFactory {
public:
  std::shared_ptr<grpc::ChannelCredentials>
  getChannelCredentials(const envoy::api::v2::core::GrpcService& grpc_service_config,
                        Api::Api& api) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() {
    return std::make_unique<envoy::config::grpc_credential::v2alpha::AwsIamConfig>();
  }

  std::string name() const override { return GrpcCredentialsNames::get().AwsIam; }

private:
  static std::string getRegion(const envoy::config::grpc_credential::v2alpha::AwsIamConfig& config);
};

/**
 * Produce AWS IAM signature metadata for a gRPC call.
 */
class AwsIamHeaderAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
  AwsIamHeaderAuthenticator(HttpFilters::Common::Aws::SignerPtr signer)
      : signer_(std::move(signer)) {}

  grpc::Status GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                           std::multimap<grpc::string, grpc::string>* metadata) override;

  bool IsBlocking() const override { return true; }

private:
  static Http::RequestMessageImpl buildMessageToSign(absl::string_view service_url,
                                                     absl::string_view method_name);

  static void signedHeadersToMetadata(const Http::HeaderMap& headers,
                                      std::multimap<grpc::string, grpc::string>& metadata);

  const HttpFilters::Common::Aws::SignerPtr signer_;
};

} // namespace AwsIam
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
