#include "source/extensions/common/aws/credential_provider_chains/custom_credentials_provider_chain.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

CustomCredentialsProviderChain::CustomCredentialsProviderChain(
    Server::Configuration::ServerFactoryContext& context, absl::string_view region,
    const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
    CustomCredentialsProviderChainFactories& factories) {

  aws_cluster_manager_ =
      context.singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
          [&context] {
            return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(context);
          },
          true);

  // Custom chain currently only supports file based and web identity credentials
  if (credential_provider_config.has_assume_role_with_web_identity_provider()) {
    auto web_identity = credential_provider_config.assume_role_with_web_identity_provider();
    std::string role_session_name = web_identity.role_session_name();
    if (role_session_name.empty()) {
      web_identity.set_role_session_name(sessionName(context.api()));
    }
    add(factories.createWebIdentityCredentialsProvider(context, aws_cluster_manager_, region,
                                                       web_identity));
  }

  if (credential_provider_config.has_credentials_file_provider()) {
    add(factories.createCredentialsFileCredentialsProvider(
        context, credential_provider_config.credentials_file_provider()));
  }
}

CredentialsProviderSharedPtr CustomCredentialsProviderChain::createWebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config) {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = stsClusterName(region);
  auto uri = Utility::getSTSEndpoint(region) + ":443";

  auto status = aws_cluster_manager.ref()->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto credential_provider = std::make_shared<WebIdentityCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, refresh_state,
      initialization_timer, web_identity_config);
  auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));

  if (handleOr.ok()) {
    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }
  return credential_provider;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
