#pragma once
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/cached_credentials_provider_base.h"
#include "source/extensions/common/aws/credential_provider_chains/credentials_provider_chain_factories.h"
#include "source/extensions/common/aws/credential_provider_chains/default_credentials_provider_chain.h"
#include "source/extensions/common/aws/credential_providers/container_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/environment_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/instance_profile_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"
#include "source/extensions/common/aws/metadata_fetcher.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 * Default AWS credentials provider chain.
 *
 * Reference implementation:
 * https://github.com/aws/aws-sdk-cpp/blob/master/aws-cpp-sdk-core/source/auth/AWSCredentialsProviderChain.cpp#L44
 */
class DefaultCredentialsProviderChain : public CredentialsProviderChain,
                                        public CredentialsProviderChainFactories {
public:
  DefaultCredentialsProviderChain(
      Api::Api& api, ServerFactoryContextOptRef context, absl::string_view region,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config =
          {})
      : DefaultCredentialsProviderChain(api, context, region, fetch_metadata_using_curl,
                                        credential_provider_config, *this) {}

  DefaultCredentialsProviderChain(
      Api::Api& api, ServerFactoryContextOptRef context, absl::string_view region,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
      CredentialsProviderChainFactories& factories);

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
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view authorization_token) override;

  CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) override;

  CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) override;

  AwsClusterManagerPtr aws_cluster_manager_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
