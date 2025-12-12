#pragma once
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/environment_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

using AwsCredentialProviderOptRef =
    OptRef<const envoy::extensions::common::aws::v3::AwsCredentialProvider>;

class CredentialsProviderChainFactories {
public:
  virtual ~CredentialsProviderChainFactories() = default;

  virtual CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const PURE;

  virtual CredentialsProviderSharedPtr createCredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config = {}) const PURE;

  virtual CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) PURE;

  virtual CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
      absl::string_view cluster_name, absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view authorization_token = {}) PURE;

  virtual CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) PURE;

  virtual CredentialsProviderSharedPtr createAssumeRoleCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider& assume_role_config)
      PURE;

  virtual CredentialsProviderSharedPtr createIAMRolesAnywhereCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider&
          iam_roles_anywhere_config) const PURE;

protected:
  std::string stsClusterName(absl::string_view region) {
    return absl::StrCat(STS_TOKEN_CLUSTER, "-", region);
  }

  std::string sessionName(Api::Api& api) {
    const auto role_session_name = absl::NullSafeStringView(std::getenv(AWS_ROLE_SESSION_NAME));
    std::string actual_session_name;
    if (!role_session_name.empty()) {
      actual_session_name = std::string(role_session_name);
    } else {
      // In practice, this value will be provided by the environment, so the placeholder value is
      // not important. Some AWS SDKs use time in nanoseconds, so we'll just use that.
      const auto now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 api.timeSource().systemTime().time_since_epoch())
                                 .count();
      actual_session_name = fmt::format("{}", now_nanos);
    }
    return actual_session_name;
  }
};

/**
 * AWS credentials provider chain.
 *
 * Reference implementation:
 * https://github.com/aws/aws-sdk-cpp/blob/master/aws-cpp-sdk-core/source/auth/AWSCredentialsProviderChain.cpp#L44
 */

class CommonCredentialsProviderChain : public CredentialsProviderChain,
                                       public CredentialsProviderChainFactories {
public:
  CommonCredentialsProviderChain(Server::Configuration::ServerFactoryContext& context,
                                 absl::string_view region,
                                 AwsCredentialProviderOptRef credential_provider_config)
      : CommonCredentialsProviderChain(context, region, credential_provider_config, *this) {}

  CommonCredentialsProviderChain(Server::Configuration::ServerFactoryContext& context,
                                 absl::string_view region,
                                 AwsCredentialProviderOptRef credential_provider_config,
                                 CredentialsProviderChainFactories& factories);

  /*
   * Create a custom credential provider chain using config provided in credential_provider_config
   */

  static absl::StatusOr<CredentialsProviderChainSharedPtr> customCredentialsProviderChain(
      Server::Configuration::ServerFactoryContext& context, absl::string_view region,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config);

  /*
   * Create the default credential provider chain
   */

  static CredentialsProviderChainSharedPtr
  defaultCredentialsProviderChain(Server::Configuration::ServerFactoryContext& context,
                                  absl::string_view region);

private:
  CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const override {
    return std::make_shared<EnvironmentCredentialsProvider>();
  }

  CredentialsProviderSharedPtr createCredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config

  ) const override {
    return std::make_shared<CredentialsFileCredentialsProvider>(context, credential_file_config);
  };

  CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
      absl::string_view cluster_name, absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view authorization_token) override;

  CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) override;

  CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) override;

  CredentialsProviderSharedPtr createAssumeRoleCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider& assume_role_config)
      override;

  CredentialsProviderSharedPtr createIAMRolesAnywhereCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerPtr aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider&
          iam_roles_anywhere_config) const override;

  AwsClusterManagerPtr aws_cluster_manager_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
