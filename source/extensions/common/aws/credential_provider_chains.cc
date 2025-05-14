#include "source/extensions/common/aws/credential_provider_chains.h"

#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "source/extensions/common/aws/credential_providers/container_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/instance_profile_credentials_provider.h"
#include "source/extensions/common/aws/credential_providers/assume_role_credentials_provider.h"
#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

SINGLETON_MANAGER_REGISTRATION(aws_cluster_manager);

CommonCredentialsProviderChain::CommonCredentialsProviderChain(
    Server::Configuration::ServerFactoryContext& context, absl::string_view region,
    AwsCredentialProviderOptRef credential_provider_config,
    CredentialsProviderChainFactories& factories) {

  envoy::extensions::common::aws::v3::AwsCredentialProvider chain_to_create;

  // If a credential provider config is provided, then we will use that as the definition of our chain
  if(credential_provider_config.has_value())
  {
    chain_to_create.CopyFrom(credential_provider_config.value());
    ENVOY_LOG(debug, "Using custom credentials provider chain");
  }
  else {
  // No chain configuration provided, so use the following providers as the default:
  //  - Environment credentials provider
  //  - Credentials file provider
  //  - Container credentials provider
  //  - Instance profile credentials provider
  //  - Assume role with web identity provider
    chain_to_create.mutable_environment_credential_provider();
    chain_to_create.mutable_credentials_file_provider();
    chain_to_create.mutable_container_credential_provider();
    chain_to_create.mutable_instance_profile_credential_provider();
    chain_to_create.mutable_assume_role_with_web_identity_provider();
    ENVOY_LOG(debug, "Using default credentials provider chain");
  }

  aws_cluster_manager_ =
      context.singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
          [&context] {
            return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(context);
          },
          true);

  if(chain_to_create.has_environment_credential_provider())
  {
    ENVOY_LOG(debug, "Using environment credentials provider");
    add(factories.createEnvironmentCredentialsProvider());
  }

  // Initial state for an async credential receiver
  auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  // Initial amount of time for async credential receivers to wait for an initial refresh to succeed
  auto initialization_timer = std::chrono::seconds(2);

  if(chain_to_create.has_credentials_file_provider())
  {
  ENVOY_LOG(debug, "Using credentials file credentials provider");
  add(factories.createCredentialsFileCredentialsProvider(
      context, chain_to_create.credentials_file_provider()));
  }

  if(chain_to_create.has_assume_role_credential_provider())
  {
      const auto& assume_role_config = chain_to_create.assume_role_credential_provider();

      const auto sts_endpoint = Utility::getSTSEndpoint(region) + ":443";
      const auto cluster_name = stsClusterName(region);

      ENVOY_LOG(debug,
                "Using assumerole credentials provider with STS endpoint: {} and session name: {}",
                sts_endpoint, assume_role_config.role_session_name());
      add(factories.createAssumeRoleCredentialsProvider(context, aws_cluster_manager_, region,
                                                        assume_role_config));
  }

  if(chain_to_create.has_assume_role_with_web_identity_provider())
  {
    auto web_identity = chain_to_create.assume_role_with_web_identity_provider();

    // Configure defaults if nothing is set in the config
    if (!web_identity.has_web_identity_token_data_source()) {
      web_identity.mutable_web_identity_token_data_source()->set_filename(
          absl::NullSafeStringView(std::getenv(AWS_WEB_IDENTITY_TOKEN_FILE)));
    }

    if (web_identity.role_arn().empty()) {
      web_identity.set_role_arn(absl::NullSafeStringView(std::getenv(AWS_ROLE_ARN)));
    }

    if (web_identity.role_session_name().empty()) {
      web_identity.set_role_session_name(sessionName(context.api()));
    }

    if ((!web_identity.web_identity_token_data_source().filename().empty() ||
        !web_identity.web_identity_token_data_source().inline_bytes().empty() ||
        !web_identity.web_identity_token_data_source().inline_string().empty() ||
        !web_identity.web_identity_token_data_source().environment_variable().empty()) &&
        !web_identity.role_arn().empty()) {

      const auto sts_endpoint = Utility::getSTSEndpoint(region) + ":443";
      const auto cluster_name = stsClusterName(region);

      ENVOY_LOG(debug,
                "Using web identity credentials provider with STS endpoint: {} and session name: {}",
                sts_endpoint, web_identity.role_session_name());
      add(factories.createWebIdentityCredentialsProvider(context, aws_cluster_manager_, region,
                                                        web_identity));
    }
  }

  if(chain_to_create.has_container_credential_provider())
  {
    const auto relative_uri =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI));
    const auto full_uri = absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI));

    if (!relative_uri.empty()) {
      const auto uri = absl::StrCat(CONTAINER_METADATA_HOST, relative_uri);
      ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", uri);
      add(factories.createContainerCredentialsProvider(
           context, aws_cluster_manager_, MetadataFetcher::create, CONTAINER_METADATA_CLUSTER,
          uri, refresh_state, initialization_timer));
    } else if (!full_uri.empty()) {
      auto authorization_token =
          absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
      if (!authorization_token.empty()) {
        ENVOY_LOG(debug,
                  "Using container role credentials provider with URI: "
                  "{} and authorization token",
                  full_uri);
        add(factories.createContainerCredentialsProvider(
             context, aws_cluster_manager_, MetadataFetcher::create, CONTAINER_METADATA_CLUSTER,
            full_uri, refresh_state, initialization_timer, authorization_token));
      } else {
        ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", full_uri);
        add(factories.createContainerCredentialsProvider(
             context, aws_cluster_manager_, MetadataFetcher::create, CONTAINER_METADATA_CLUSTER,
            full_uri, refresh_state, initialization_timer));
      }
    } 
  }
  const auto metadata_disabled = absl::NullSafeStringView(std::getenv(AWS_EC2_METADATA_DISABLED));

  if (chain_to_create.has_instance_profile_credential_provider() && metadata_disabled != "true") {
      ENVOY_LOG(debug, "Using instance profile credentials provider");
      add(factories.createInstanceProfileCredentialsProvider(
           context, aws_cluster_manager_, MetadataFetcher::create, refresh_state,
          initialization_timer, EC2_METADATA_CLUSTER));
  }
}

SINGLETON_MANAGER_REGISTRATION(container_credentials_provider);
SINGLETON_MANAGER_REGISTRATION(instance_profile_credentials_provider);

CredentialsProviderSharedPtr CommonCredentialsProviderChain::createAssumeRoleCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view region,
    const envoy::extensions::common::aws::v3::AssumeRoleCredentialProvider&
        assume_role_config) {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = stsClusterName(region);
  auto uri = Utility::getSTSEndpoint(region) + ":443";

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  envoy::extensions::common::aws::v3::AwsCredentialProvider defaults;
  envoy::extensions::common::aws::v3::InstanceProfileCredentialProvider ins;
  defaults.mutable_instance_profile_credential_provider()->CopyFrom(ins);
      auto credentials_provider_chain =
  std::make_shared<Extensions::Common::Aws::CommonCredentialsProviderChain>(
      context, region, defaults);
  const auto matcher_config = Extensions::Common::Aws::AwsSigningHeaderExclusionVector{};

  auto signer = std::make_unique<SigV4SignerImpl>(
      STS_SERVICE_NAME, region, credentials_provider_chain, context, matcher_config);
      
  auto credential_provider = std::make_shared<AssumeRoleCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, region, refresh_state,
      initialization_timer, std::move(signer), assume_role_config);

  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));

  if (handleOr.ok()) {
    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
};

CredentialsProviderSharedPtr CommonCredentialsProviderChain::createContainerCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    absl::string_view cluster_name, absl::string_view credential_uri,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view authorization_token = {}) {

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::STATIC, credential_uri);

  auto credential_provider =
      context.singletonManager()
          .getTyped<Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
              SINGLETON_MANAGER_REGISTERED_NAME(container_credentials_provider),
              [&context, &aws_cluster_manager, create_metadata_fetcher_cb, &credential_uri,
               &refresh_state, &initialization_timer, &authorization_token, &cluster_name] {
                return std::make_shared<
                    Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
                    context, aws_cluster_manager, create_metadata_fetcher_cb, credential_uri,
                    refresh_state, initialization_timer, authorization_token, cluster_name);
              });

  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
  if (handleOr.ok()) {
    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
}

CredentialsProviderSharedPtr
CommonCredentialsProviderChain::createInstanceProfileCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context,
    AwsClusterManagerPtr aws_cluster_manager, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view cluster_name) {

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::STATIC, EC2_METADATA_HOST);
  auto credential_provider =
      context.singletonManager()
          .getTyped<Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
              SINGLETON_MANAGER_REGISTERED_NAME(instance_profile_credentials_provider),
              [&context, &aws_cluster_manager, create_metadata_fetcher_cb, &refresh_state,
               &initialization_timer, &cluster_name] {
                return std::make_shared<
                    Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
                    context, aws_cluster_manager, create_metadata_fetcher_cb, refresh_state,
                    initialization_timer, cluster_name);
              });

  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
  if (handleOr.ok()) {

    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
}

CredentialsProviderSharedPtr CommonCredentialsProviderChain::createWebIdentityCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view region,
    const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
        web_identity_config) {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = stsClusterName(region);
  auto uri = Utility::getSTSEndpoint(region) + ":443";

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto credential_provider = std::make_shared<WebIdentityCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, refresh_state,
      initialization_timer, web_identity_config);
  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));

  if (handleOr.ok()) {

    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
