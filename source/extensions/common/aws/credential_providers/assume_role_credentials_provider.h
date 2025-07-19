#pragma once

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signers/sigv4_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char ASSUMEROLE_RESPONSE_ELEMENT[] = "AssumeRoleResponse";
constexpr char ASSUMEROLE_RESULT_ELEMENT[] = "AssumeRoleResult";

class AssumeRoleCredentialsProvider : public MetadataCredentialsProviderBase,
                                      public MetadataFetcher::MetadataReceiver {
public:
  AssumeRoleCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view region,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer,
      envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider assume_role_config);

  std::string providerName() override { return "AssumeRoleCredentialsProvider"; };

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

private:
  void refresh() override;
  void fetchCredentialFromRolesAnywhere(const std::string&& instance_role,
                                        const std::string&& token);
  void extractCredentials(const std::string&& credential_document_value);

  void continueRefresh();

  const std::string role_arn_;
  const std::string role_session_name_;
  const std::string region_;
  const std::string external_id_;
  absl::optional<uint16_t> session_duration_;
  std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer_;
};

using AssumeRoleCredentialsProviderPtr = std::shared_ptr<AssumeRoleCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
