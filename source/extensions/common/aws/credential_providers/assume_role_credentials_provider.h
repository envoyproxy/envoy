#pragma once

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// IAM Roles Anywhere credential strings
constexpr char CREDENTIAL_SET[] = "credentialSet";
constexpr char CREDENTIALS_LOWER[] = "credentials";
constexpr char ACCESS_KEY_ID_LOWER[] = "accessKeyId";
constexpr char SECRET_ACCESS_KEY_LOWER[] = "secretAccessKey";
constexpr char EXPIRATION_LOWER[] = "expiration";
constexpr char SESSION_TOKEN_LOWER[] = "sessionToken";

constexpr char ROLESANYWHERE_SERVICE[] = "rolesanywhere";
constexpr char EXPIRATION_FORMAT[] = "%E4Y-%m-%dT%H:%M:%S%z";
/**
 *
 * AssumeRoleCredentialsProvider purpose is to Exchange X509 Credentials for Temporary AWS
 * Credentials
 *
 * When instantiated via config, it will create an IAMRolesAnywhereX509CredentialsProvider, which
 * manages the X509 Credentials. AssumeRoleCredentialsProvider works in the same way as
 * WebIdentityCredentialsProvider, by using the async HTTP client to send requests to the AWS IAM
 * Roles Anywhere service and retrieve temporary AWS Credentials. It is therefore as subclass of
 * MetadataCredentialsProviderBase, which handles the async credential fetch and cluster creation
 * for the IAM Roles Anywhere endpoint.
 *
 * The X509 SigV4 signing process is performed via IAMRolesAnywhereSigV4Signer, which is a
 * modification of standard SigV4 signing to use X509 credentials as the signing input.
 * AssumeRoleCredentialsProvider is the only consumer of IAMRolesAnywhereSigV4Signer.
 *
 * The logic is as follows:
 *   1. IAMRolesAnywhereX509CredentialsProvider retrieves X509 credentials and converts them to
 * required format
 *
 *   2. AssumeRoleCredentialsProvider uses credentials from
 * IAMRolesAnywhereX509CredentialsProvider, and uses them as input to
 * IAMRolesAnywhereSigV4Signer
 *
 *   3. Once signing has completed, AssumeRoleCredentialsProvider requests temporary
 * credentials from IAM Roles Anywhere endpoint using the signed payload
 *
 *   4. Temporary credentials are returned, which then can be used in normal AWS SigV4/SigV4A
 * signing
 *
 */

class AssumeRoleCredentialsProvider : public MetadataCredentialsProviderBase,
                                            public MetadataFetcher::MetadataReceiver {
public:
  AssumeRoleCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view region,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer,
      envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider
          iam_roles_anywhere_config);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;
  std::string providerName() override { return "AssumeRoleCredentialsProvider"; };

private:
  bool needsRefresh() override;
  void refresh() override;
  void fetchCredentialFromRolesAnywhere(const std::string&& instance_role,
                                        const std::string&& token);
  void extractCredentials(const std::string&& credential_document_value);

  const std::string role_arn_;
  const std::string role_session_name_;
  const std::string region_;
  absl::optional<uint16_t> session_duration_;
  ServerFactoryContextOptRef server_factory_context_;
  std::unique_ptr<Extensions::Common::Aws::SigV4SignerImpl> assume_role_signer_;
};

using AssumeRoleCredentialsProviderPtr = std::shared_ptr<AssumeRoleCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
