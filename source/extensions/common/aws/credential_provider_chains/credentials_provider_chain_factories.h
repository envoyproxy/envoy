#pragma once
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) PURE;

  virtual CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view authorization_token = {}) PURE;

  virtual CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) PURE;

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

class CustomCredentialsProviderChainFactories {
public:
  virtual ~CustomCredentialsProviderChainFactories() = default;

  virtual CredentialsProviderSharedPtr createCredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config = {}) const PURE;

  virtual CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) PURE;
};

SINGLETON_MANAGER_REGISTRATION(aws_cluster_manager);

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
