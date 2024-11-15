#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/grpc_credential/v3/aws_iam.pb.h"
#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/http/header_map.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/signer.h"

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
  getChannelCredentials(const envoy::config::core::v3::GrpcService& grpc_service_config,
                        Server::Configuration::CommonFactoryContext& context) override;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() {
    return std::make_unique<envoy::config::grpc_credential::v3::AwsIamConfig>();
  }

  std::string name() const override { return "envoy.grpc_credentials.aws_iam"; }
};

/**
 * Produce AWS IAM signature metadata for a gRPC call.
 */
class AwsIamHeaderAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
  AwsIamHeaderAuthenticator(Common::Aws::SignerPtr signer) : signer_(std::move(signer)) {}

  grpc::Status GetMetadata(grpc::string_ref, grpc::string_ref, const grpc::AuthContext&,
                           std::multimap<grpc::string, grpc::string>* metadata) override;

  bool IsBlocking() const override { return true; }

private:
  static Http::RequestMessageImpl buildMessageToSign(absl::string_view service_url,
                                                     absl::string_view method_name);

  static void signedHeadersToMetadata(const Http::HeaderMap& headers,
                                      std::multimap<grpc::string, grpc::string>& metadata);

  const Common::Aws::SignerPtr signer_;
};

} // namespace AwsIam
} // namespace GrpcCredentials
} // namespace Extensions
} // namespace Envoy
