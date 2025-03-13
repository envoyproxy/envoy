#include "source/extensions/common/aws/credential_provider_chains.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

SINGLETON_MANAGER_REGISTRATION(aws_cluster_manager);

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

  // Custom chain currently only supports iam roles anywhere, credentials file and web identity
  // credentials
  if (credential_provider_config.has_iam_roles_anywhere_provider()) {
    ENVOY_LOG(debug, "Using IAM Roles Anywhere credentials provider");
    add(factories.createIAMRolesAnywhereCredentialsProvider(
        context, aws_cluster_manager_, region,
        credential_provider_config.iam_roles_anywhere_provider()));
  }

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

DefaultCredentialsProviderChain::DefaultCredentialsProviderChain(
    Api::Api& api, ServerFactoryContextOptRef context, absl::string_view region,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
    CredentialsProviderChainFactories& factories) {

  if (context) {
    aws_cluster_manager_ =
        context->singletonManager().getTyped<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
            SINGLETON_MANAGER_REGISTERED_NAME(aws_cluster_manager),
            [&context] {
              return std::make_shared<Envoy::Extensions::Common::Aws::AwsClusterManagerImpl>(
                  context.value());
            },
            true);
  }

  ENVOY_LOG(debug, "Using environment credentials provider");
  add(factories.createEnvironmentCredentialsProvider());

  // Initial state for an async credential receiver
  auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  // Initial amount of time for async credential receivers to wait for an initial refresh to succeed
  auto initialization_timer = std::chrono::seconds(2);

  if (context) {

    ENVOY_LOG(debug, "Using credentials file credentials provider");
    add(factories.createCredentialsFileCredentialsProvider(
        context.value(), credential_provider_config.credentials_file_provider()));

    if (credential_provider_config.has_iam_roles_anywhere_provider() && context) {
      ENVOY_LOG(debug, "Using IAM Roles Anywhere credentials provider");
      add(factories.createIAMRolesAnywhereCredentialsProvider(
          context.value(), aws_cluster_manager_, region,
          credential_provider_config.iam_roles_anywhere_provider()));
    }

    auto web_identity = credential_provider_config.assume_role_with_web_identity_provider();

    // Configure defaults if nothing is set in the config
    if (!web_identity.has_web_identity_token_data_source()) {
      web_identity.mutable_web_identity_token_data_source()->set_filename(
          absl::NullSafeStringView(std::getenv(AWS_WEB_IDENTITY_TOKEN_FILE)));
    }

    if (web_identity.role_arn().empty()) {
      web_identity.set_role_arn(absl::NullSafeStringView(std::getenv(AWS_ROLE_ARN)));
    }

    if (web_identity.role_session_name().empty()) {
      web_identity.set_role_session_name(sessionName(api));
    }

    if ((!web_identity.web_identity_token_data_source().filename().empty() ||
         !web_identity.web_identity_token_data_source().inline_bytes().empty() ||
         !web_identity.web_identity_token_data_source().inline_string().empty() ||
         !web_identity.web_identity_token_data_source().environment_variable().empty()) &&
        !web_identity.role_arn().empty()) {

      const auto sts_endpoint = Utility::getSTSEndpoint(region) + ":443";
      const auto cluster_name = stsClusterName(region);

      ENVOY_LOG(
          debug,
          "Using web identity credentials provider with STS endpoint: {} and session name: {}",
          sts_endpoint, web_identity.role_session_name());
      add(factories.createWebIdentityCredentialsProvider(context.value(), aws_cluster_manager_,
                                                         region, web_identity));
    }
  }

  // Even if WebIdentity is supported keep the fallback option open so that
  // Envoy can use other credentials provider if available.
  const auto relative_uri =
      absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_RELATIVE_URI));
  const auto full_uri = absl::NullSafeStringView(std::getenv(AWS_CONTAINER_CREDENTIALS_FULL_URI));
  const auto metadata_disabled = absl::NullSafeStringView(std::getenv(AWS_EC2_METADATA_DISABLED));

  if (!relative_uri.empty()) {
    const auto uri = absl::StrCat(CONTAINER_METADATA_HOST, relative_uri);
    ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", uri);
    add(factories.createContainerCredentialsProvider(
        api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
        MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, uri, refresh_state,
        initialization_timer));
  } else if (!full_uri.empty()) {
    auto authorization_token =
        absl::NullSafeStringView(std::getenv(AWS_CONTAINER_AUTHORIZATION_TOKEN));
    if (!authorization_token.empty()) {
      ENVOY_LOG(debug,
                "Using container role credentials provider with URI: "
                "{} and authorization token",
                full_uri);
      add(factories.createContainerCredentialsProvider(
          api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
          MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, full_uri, refresh_state,
          initialization_timer, authorization_token));
    } else {
      ENVOY_LOG(debug, "Using container role credentials provider with URI: {}", full_uri);
      add(factories.createContainerCredentialsProvider(
          api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
          MetadataFetcher::create, CONTAINER_METADATA_CLUSTER, full_uri, refresh_state,
          initialization_timer));
    }
  } else if (metadata_disabled != "true") {
    ENVOY_LOG(debug, "Using instance profile credentials provider");
    add(factories.createInstanceProfileCredentialsProvider(
        api, context, makeOptRef(aws_cluster_manager_), fetch_metadata_using_curl,
        MetadataFetcher::create, refresh_state, initialization_timer, EC2_METADATA_CLUSTER));
  }
}

SINGLETON_MANAGER_REGISTRATION(container_credentials_provider);
SINGLETON_MANAGER_REGISTRATION(instance_profile_credentials_provider);

CredentialsProviderSharedPtr DefaultCredentialsProviderChain::createContainerCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
    absl::string_view credential_uri, MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view authorization_token = {}) {

  // TODO: @nbaws Remove curl path post deprecation
  if (!context) {
    return std::make_shared<ContainerCredentialsProvider>(
        api, context, absl::nullopt, fetch_metadata_using_curl, create_metadata_fetcher_cb,
        credential_uri, refresh_state, initialization_timer, authorization_token, cluster_name);
  } else {

    auto status = aws_cluster_manager.ref()->addManagedCluster(
        cluster_name, envoy::config::cluster::v3::Cluster::STATIC, credential_uri);

    auto credential_provider =
        context->singletonManager()
            .getTyped<Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
                SINGLETON_MANAGER_REGISTERED_NAME(container_credentials_provider),
                [&context, &api, &aws_cluster_manager, fetch_metadata_using_curl,
                 create_metadata_fetcher_cb, &credential_uri, &refresh_state, &initialization_timer,
                 &authorization_token, &cluster_name] {
                  return std::make_shared<
                      Envoy::Extensions::Common::Aws::ContainerCredentialsProvider>(
                      api, context, aws_cluster_manager, fetch_metadata_using_curl,
                      create_metadata_fetcher_cb, credential_uri, refresh_state,
                      initialization_timer, authorization_token, cluster_name);
                });

    auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
        cluster_name,
        *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
    if (handleOr.ok()) {
      credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
    }

    storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

    return credential_provider;
  }
}

CredentialsProviderSharedPtr
DefaultCredentialsProviderChain::createInstanceProfileCredentialsProvider(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer, absl::string_view cluster_name) {

  if (!context) {
    return std::make_shared<InstanceProfileCredentialsProvider>(
        api, context, absl::nullopt, fetch_metadata_using_curl, create_metadata_fetcher_cb,
        refresh_state, initialization_timer, cluster_name);
  } else {

    auto status = aws_cluster_manager.ref()->addManagedCluster(
        cluster_name, envoy::config::cluster::v3::Cluster::STATIC, EC2_METADATA_HOST);
    auto credential_provider =
        context->singletonManager()
            .getTyped<Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
                SINGLETON_MANAGER_REGISTERED_NAME(instance_profile_credentials_provider),
                [&context, &api, &aws_cluster_manager, fetch_metadata_using_curl,
                 create_metadata_fetcher_cb, &refresh_state, &initialization_timer, &cluster_name] {
                  return std::make_shared<
                      Envoy::Extensions::Common::Aws::InstanceProfileCredentialsProvider>(
                      api, context, aws_cluster_manager, fetch_metadata_using_curl,
                      create_metadata_fetcher_cb, refresh_state, initialization_timer,
                      cluster_name);
                });

    auto handleOr = aws_cluster_manager.ref()->addManagedClusterUpdateCallbacks(
        cluster_name,
        *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
    if (handleOr.ok()) {

      credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
    }

    storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

    return credential_provider;
  }
}

CredentialsProviderSharedPtr DefaultCredentialsProviderChain::createWebIdentityCredentialsProvider(
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

  storeSubscription(credential_provider->subscribeToCredentialUpdates(*this));

  return credential_provider;
};

CredentialsProviderSharedPtr
DefaultCredentialsProviderChain::createIAMRolesAnywhereCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view region,
    const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider&
        iam_roles_anywhere_config) const {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = Utility::getRolesAnywhereEndpoint(region);
  auto uri = cluster_name + ":443";

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto roles_anywhere_certificate_provider =
      std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
          context, iam_roles_anywhere_config.certificate(), iam_roles_anywhere_config.private_key(),
          iam_roles_anywhere_config.certificate_chain());
  // Create our own x509 signer just for IAM Roles Anywhere
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view(region),
          roles_anywhere_certificate_provider, context.mainThreadDispatcher().timeSource());

  auto credential_provider = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, region, refresh_state,
      initialization_timer, std::move(roles_anywhere_signer), iam_roles_anywhere_config);
  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
      cluster_name,
      *std::dynamic_pointer_cast<AwsManagedClusterUpdateCallbacks>(credential_provider));
  if (handleOr.ok()) {

    credential_provider->setClusterReadyCallbackHandle(std::move(handleOr.value()));
  }
  return credential_provider;
};

CredentialsProviderSharedPtr
CustomCredentialsProviderChain::createIAMRolesAnywhereCredentialsProvider(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view region,
    const envoy::extensions::common::aws::v3::IAMRolesAnywhereCredentialProvider&
        iam_roles_anywhere_config) const {

  const auto refresh_state = MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh;
  const auto initialization_timer = std::chrono::seconds(2);

  auto cluster_name = Utility::getRolesAnywhereEndpoint(region);
  auto uri = cluster_name + ":443";

  auto status = aws_cluster_manager->addManagedCluster(
      cluster_name, envoy::config::cluster::v3::Cluster::LOGICAL_DNS, uri);

  auto roles_anywhere_certificate_provider =
      std::make_shared<IAMRolesAnywhereX509CredentialsProvider>(
          context, iam_roles_anywhere_config.certificate(), iam_roles_anywhere_config.private_key(),
          iam_roles_anywhere_config.certificate_chain());
  // Create our own x509 signer just for IAM Roles Anywhere
  auto roles_anywhere_signer =
      std::make_unique<Extensions::Common::Aws::IAMRolesAnywhereSigV4Signer>(
          absl::string_view(ROLESANYWHERE_SERVICE), absl::string_view(region),
          roles_anywhere_certificate_provider, context.mainThreadDispatcher().timeSource());

  auto credential_provider = std::make_shared<IAMRolesAnywhereCredentialsProvider>(
      context, aws_cluster_manager, cluster_name, MetadataFetcher::create, region, refresh_state,
      initialization_timer, std::move(roles_anywhere_signer), iam_roles_anywhere_config);

  auto handleOr = aws_cluster_manager->addManagedClusterUpdateCallbacks(
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
