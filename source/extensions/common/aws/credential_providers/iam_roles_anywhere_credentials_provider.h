#pragma once

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/signers/iam_roles_anywhere_sigv4_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char ROLESANYWHERE_SERVICE[] = "rolesanywhere";

/**
 *
 * IAMRolesAnywhereCredentialsProvider purpose is to exchange X509 Credentials for Temporary AWS
 * Credentials
 *
 * When instantiated via config, it will create an IAMRolesAnywhereX509CredentialsProvider, which
 * manages the X509 Credentials. IAMRolesAnywhereCredentialsProvider works in the same way as
 * WebIdentityCredentialsProvider, by using the async HTTP client to send requests to the AWS IAM
 * Roles Anywhere service and retrieve temporary AWS Credentials. It is therefore a subclass of
 * MetadataCredentialsProviderBase, which handles the async credential fetch and cluster creation
 * for the IAM Roles Anywhere endpoint.
 *
 * The X509 SigV4 signing process is performed via IAMRolesAnywhereSigV4Signer, which is a
 * modification of standard SigV4 signing to use X509 credentials as the signing input.
 * IAMRolesAnywhereCredentialsProvider is the only consumer of IAMRolesAnywhereSigV4Signer.
 *
 * The logic is as follows:
 *   1. IAMRolesAnywhereX509CredentialsProvider retrieves X509 credentials and converts them to
 * required format
 *
 *   2. IAMRolesAnywhereCredentialsProvider uses credentials from
 * IAMRolesAnywhereX509CredentialsProvider, and uses them as input to
 * IAMRolesAnywhereSigV4Signer
 *
 *   3. Once signing has completed, IAMRolesAnywhereCredentialsProvider requests temporary
 * credentials from IAM Roles Anywhere endpoint using the signed payload
 *
 *   4. Temporary credentials are returned, which then can be used in normal AWS SigV4/SigV4A
 * signing
 *
 */

class IAMRolesAnywhereCredentialsProvider : public MetadataCredentialsProviderBase,
                                            public MetadataFetcher::MetadataReceiver {
public:
  IAMRolesAnywhereCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view region,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      std::unique_ptr<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer> roles_anywhere_signer,
      const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider
          iam_roles_anywhere_config);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;
  std::string providerName() override { return "IAMRolesAnywhereCredentialsProvider"; };

private:
  void refresh() override;
  void extractCredentials(const std::string&& credential_document_value);

  const std::string role_arn_;
  const std::string role_session_name_;
  const std::string profile_arn_;
  const std::string trust_anchor_arn_;
  const std::string region_;
  absl::optional<uint16_t> session_duration_;
  std::unique_ptr<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer> roles_anywhere_signer_;
};

using IAMRolesAnywhereCredentialsProviderPtr = std::shared_ptr<IAMRolesAnywhereCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
