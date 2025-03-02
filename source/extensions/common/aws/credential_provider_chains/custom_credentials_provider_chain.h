#pragma once
#include "source/extensions/common/aws/aws_cluster_manager.h"

// #include "source/extensions/common/aws/metadata_fetcher.h"
// #include "source/extensions/common/aws/cached_credentials_provider_base.h"
// #include "source/extensions/common/aws/metadata_credentials_provider_base.h"
// #include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
// #include
// "source/extensions/common/aws/credential_provider_chains/default_credentials_provider_chain.h"
#include "source/extensions/common/aws/credential_provider_chains/credentials_provider_chain_factories.h"
// #include "source/extensions/common/aws/credential_providers/webidentity_credentials_provider.h"
// #include "source/extensions/common/aws/credential_providers/environment_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/credentials_file_credentials_provider.h"
// #include "source/extensions/common/aws/credential_providers/container_credentials_provider.h"
// #include
// "source/extensions/common/aws/credential_providers/instance_profile_credentials_provider.h"
// #include "source/extensions/common/aws/utility.h"
#include "source/extensions/common/aws/credentials_provider.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// TODO(nbaws) Add additional providers to the custom chain.
class CustomCredentialsProviderChain : public CredentialsProviderChain,
                                       public CustomCredentialsProviderChainFactories {
public:
  CustomCredentialsProviderChain(
      Server::Configuration::ServerFactoryContext& context, absl::string_view region,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
      CustomCredentialsProviderChainFactories& factories);

  CustomCredentialsProviderChain(
      Server::Configuration::ServerFactoryContext& context, absl::string_view region,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config)
      : CustomCredentialsProviderChain(context, region, credential_provider_config, *this) {}

  CredentialsProviderSharedPtr createCredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config = {}

  ) const override {

    return std::make_shared<CredentialsFileCredentialsProvider>(context, credential_file_config);
  };

  CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) override;

protected:
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
  std::string stsClusterName(absl::string_view region) {
    return absl::StrCat(STS_TOKEN_CLUSTER, "-", region);
  }

  AwsClusterManagerPtr aws_cluster_manager_;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
