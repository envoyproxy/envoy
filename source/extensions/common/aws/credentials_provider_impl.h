#pragma once

#include <list>
#include <memory>
#include <optional>
#include <string>

#include "envoy/api/api.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/http/message.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/datasource.h"
#include "source/common/init/target_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

/**
 *  CreateMetadataFetcherCb is a callback interface for creating a MetadataFetcher instance.
 */
using CreateMetadataFetcherCb =
    std::function<MetadataFetcherPtr(Upstream::ClusterManager&, absl::string_view)>;
using ServerFactoryContextOptRef = OptRef<Server::Configuration::ServerFactoryContext>;

/**
 * Retrieve AWS credentials from the environment variables.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class EnvironmentCredentialsProvider : public CredentialsProvider,
                                       public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override;
};

/**
 * Returns AWS credentials from static filter configuration.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-envvars.html
 */
class ConfigCredentialsProvider : public CredentialsProvider,
                                  public Logger::Loggable<Logger::Id::aws> {
public:
  ConfigCredentialsProvider(absl::string_view access_key_id = absl::string_view(),
                            absl::string_view secret_access_key = absl::string_view(),
                            absl::string_view session_token = absl::string_view())
      : credentials_(access_key_id, secret_access_key, session_token) {}
  Credentials getCredentials() override;

private:
  const Credentials credentials_;
};

class CachedCredentialsProviderBase : public CredentialsProvider,
                                      public Logger::Loggable<Logger::Id::aws> {
public:
  Credentials getCredentials() override {
    refreshIfNeeded();
    return cached_credentials_;
  }

protected:
  SystemTime last_updated_;
  Credentials cached_credentials_;

  void refreshIfNeeded();

  virtual bool needsRefresh() PURE;
  virtual void refresh() PURE;
};

/**
 * Retrieve AWS credentials from the credentials file.
 *
 * Adheres to conventions specified in:
 * https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html
 */
class CredentialsFileCredentialsProvider : public CachedCredentialsProviderBase {
public:
  CredentialsFileCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
          credential_file_config = {});

private:
  Server::Configuration::ServerFactoryContext& context_;
  std::string profile_;
  absl::optional<Config::DataSource::DataSourceProviderPtr> credential_file_data_source_provider_;
  bool has_watched_directory_ = false;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(absl::string_view credentials_string, absl::string_view profile);
};

#define ALL_METADATACREDENTIALSPROVIDER_STATS(COUNTER, GAUGE)                                      \
  COUNTER(credential_refreshes_performed)                                                          \
  COUNTER(credential_refreshes_failed)                                                             \
  COUNTER(credential_refreshes_succeeded)                                                          \
  GAUGE(metadata_refresh_state, Accumulate)

struct MetadataCredentialsProviderStats {
  ALL_METADATACREDENTIALSPROVIDER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class MetadataCredentialsProviderBase : public CachedCredentialsProviderBase,
                                        public AwsManagedClusterUpdateCallbacks {
public:
  friend class MetadataCredentialsProviderBaseFriend;
  using CurlMetadataFetcher = std::function<absl::optional<std::string>(Http::RequestMessage&)>;
  using OnAsyncFetchCb = std::function<void(const std::string&&)>;

  MetadataCredentialsProviderBase(Api::Api& api, ServerFactoryContextOptRef context,
                                  AwsClusterManagerOptRef aws_cluster_manager,
                                  absl::string_view cluster_name,
                                  const CurlMetadataFetcher& fetch_metadata_using_curl,
                                  CreateMetadataFetcherCb create_metadata_fetcher_cb,
                                  MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                                  std::chrono::seconds initialization_timer);

  Credentials getCredentials() override;

  // Get the Metadata credentials cache duration.
  static std::chrono::seconds getCacheDuration();

  // Store the RAII cluster callback handle following registration call with AWS cluster manager
  void setClusterReadyCallbackHandle(AwsManagedClusterUpdateCallbacksHandlePtr handle) {
    callback_handle_ = std::move(handle);
  }

protected:
  struct ThreadLocalCredentialsCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCredentialsCache() : credentials_(std::make_shared<Credentials>()){};

    // The credentials object.
    CredentialsConstSharedPtr credentials_;
    // Lock guard.
    Thread::MutexBasicLockable lock_;
  };

  const std::string& clusterName() const { return cluster_name_; }

  // Callback from AWS cluster manager, triggered when our cluster comes online
  void onClusterAddOrUpdate() override;

  // Handle fetch done.
  void handleFetchDone();

  // Set Credentials shared_ptr on all threads.
  void setCredentialsToAllThreads(CredentialsConstUniquePtr&& creds);

  Api::Api& api_;
  // The optional server factory context.
  ServerFactoryContextOptRef context_;
  // Store the method to fetch metadata from libcurl (deprecated)
  CurlMetadataFetcher fetch_metadata_using_curl_;
  // The callback used to create a MetadataFetcher instance.
  CreateMetadataFetcherCb create_metadata_fetcher_cb_;
  // The cluster name to use for internal static cluster pointing towards the credentials provider.
  std::string cluster_name_;
  // The cache duration of the fetched credentials.
  std::chrono::seconds cache_duration_;
  // Metadata receiver state, describing where we are along the initial credential refresh process
  MetadataFetcher::MetadataReceiver::RefreshState refresh_state_;
  // Metadata receiver initialization timer - number of seconds between retries during the first
  // credential retrieval process
  std::chrono::seconds initialization_timer_;
  // The timer to trigger fetch due to cache duration.
  Envoy::Event::TimerPtr cache_duration_timer_;
  // The Metadata fetcher object.
  MetadataFetcherPtr metadata_fetcher_;
  // Callback function to call on successful metadata fetch.
  OnAsyncFetchCb on_async_fetch_cb_;
  // To determine if credentials fetching can continue even after metadata fetch failure.
  bool continue_on_async_fetch_failure_ = false;
  // Reason to log on fetch failure while continue.
  std::string continue_on_async_fetch_failure_reason_ = "";
  // Last update time to determine expiration.
  SystemTime last_updated_;
  // Cache credentials when using libcurl.
  Credentials cached_credentials_;
  // The expiration time received in any returned token
  absl::optional<SystemTime> expiration_time_;
  // Tls slot
  ThreadLocal::TypedSlotPtr<ThreadLocalCredentialsCache> tls_slot_ = nullptr;
  // Stats scope
  Stats::ScopeSharedPtr scope_ = nullptr;
  // Pointer to our stats structure
  std::shared_ptr<MetadataCredentialsProviderStats> stats_;
  // AWS Cluster Manager for creating clusters and retrieving URIs when async fetch is needed
  AwsClusterManagerOptRef aws_cluster_manager_;
  // RAII handle for callbacks from AWS cluster manager
  AwsManagedClusterUpdateCallbacksHandlePtr callback_handle_;
};

/**
 * Retrieve AWS credentials from the instance metadata.
 *
 * https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html#instance-metadata-security-credentials
 */
class InstanceProfileCredentialsProvider : public MetadataCredentialsProviderBase,
                                           public Envoy::Singleton::Instance,
                                           public MetadataFetcher::MetadataReceiver {
public:
  InstanceProfileCredentialsProvider(Api::Api& api, ServerFactoryContextOptRef context,
                                     AwsClusterManagerOptRef aws_cluster_manager,
                                     const CurlMetadataFetcher& fetch_metadata_using_curl,
                                     CreateMetadataFetcherCb create_metadata_fetcher_cb,
                                     MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                                     std::chrono::seconds initialization_timer,
                                     absl::string_view cluster_name);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

private:
  bool needsRefresh() override;
  void refresh() override;
  void fetchInstanceRole(const std::string&& token, bool async = false);
  void fetchInstanceRoleAsync(const std::string&& token) {
    fetchInstanceRole(std::move(token), true);
  }
  void fetchCredentialFromInstanceRole(const std::string&& instance_role, const std::string&& token,
                                       bool async = false);
  void fetchCredentialFromInstanceRoleAsync(const std::string&& instance_role,
                                            const std::string&& token) {
    fetchCredentialFromInstanceRole(std::move(instance_role), std::move(token), true);
  }
  void extractCredentials(const std::string&& credential_document_value, bool async = false);
  void extractCredentialsAsync(const std::string&& credential_document_value) {
    extractCredentials(std::move(credential_document_value), true);
  }
};

/**
 * Retrieve AWS credentials from the task metadata.
 *
 * https://docs.aws.amazon.com/AmazonECS/latest/developerguide/task-iam-roles.html#enable_task_iam_roles
 */
class ContainerCredentialsProvider : public MetadataCredentialsProviderBase,
                                     public Envoy::Singleton::Instance,
                                     public MetadataFetcher::MetadataReceiver {
public:
  ContainerCredentialsProvider(Api::Api& api, ServerFactoryContextOptRef context,
                               AwsClusterManagerOptRef aws_cluster_manager,
                               const CurlMetadataFetcher& fetch_metadata_using_curl,
                               CreateMetadataFetcherCb create_metadata_fetcher_cb,
                               absl::string_view credential_uri,
                               MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                               std::chrono::seconds initialization_timer,
                               absl::string_view authorization_token,
                               absl::string_view cluster_name);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

private:
  const std::string credential_uri_;
  const std::string authorization_token_;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(const std::string&& credential_document_value);
};

/**
 * Retrieve AWS credentials from Security Token Service using a web identity token (e.g. OAuth,
 * OpenID)
 */
class WebIdentityCredentialsProvider : public MetadataCredentialsProviderBase,
                                       public MetadataFetcher::MetadataReceiver {
public:
  // token and token_file_path are mutually exclusive. If token is not empty, token_file_path is
  // not used, and vice versa.
  WebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view cluster_name,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

private:
  const std::string sts_endpoint_;
  absl::optional<Config::DataSource::DataSourceProviderPtr> web_identity_data_source_provider_;
  const std::string role_arn_;
  const std::string role_session_name_;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(const std::string&& credential_document_value);
};

/**
 * AWS credentials provider chain, able to fallback between multiple credential providers.
 */
class CredentialsProviderChain : public CredentialsProvider,
                                 public Logger::Loggable<Logger::Id::aws> {
public:
  ~CredentialsProviderChain() override = default;

  void add(const CredentialsProviderSharedPtr& credentials_provider) {
    providers_.emplace_back(credentials_provider);
  }

  Credentials getCredentials() override;

protected:
  std::list<CredentialsProviderSharedPtr> providers_;
};

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
          web_identity_config) const PURE;

  virtual CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      absl::string_view authorization_token = {}) const PURE;

  virtual CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) const PURE;
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
          web_identity_config) const PURE;
};

// TODO(nbaws) Add additional providers to the custom chain.
class CustomCredentialsProviderChain : public CredentialsProviderChain,
                                       public CustomCredentialsProviderChainFactories {
public:
  CustomCredentialsProviderChain(
      Server::Configuration::ServerFactoryContext& context, absl::string_view region,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
      const CustomCredentialsProviderChainFactories& factories);

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
          web_identity_config) const override;

  AwsClusterManagerPtr aws_cluster_manager_;
};

/**
 * Credential provider based on an inline credential.
 */
class InlineCredentialProvider : public CredentialsProvider {
public:
  explicit InlineCredentialProvider(absl::string_view access_key_id,
                                    absl::string_view secret_access_key,
                                    absl::string_view session_token)
      : credentials_(access_key_id, secret_access_key, session_token) {}

  Credentials getCredentials() override { return credentials_; }

private:
  const Credentials credentials_;
};

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
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      absl::string_view region,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config =
          {})
      : DefaultCredentialsProviderChain(api, context, singleton_manager, region,
                                        fetch_metadata_using_curl, credential_provider_config,
                                        *this) {}

  DefaultCredentialsProviderChain(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      absl::string_view region,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      const envoy::extensions::common::aws::v3::AwsCredentialProvider& credential_provider_config,
      const CredentialsProviderChainFactories& factories);

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
      std::chrono::seconds initialization_timer,
      absl::string_view authorization_token) const override;

  CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      AwsClusterManagerOptRef aws_cluster_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) const override;

  CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Server::Configuration::ServerFactoryContext& context,
      AwsClusterManagerOptRef aws_cluster_manager, absl::string_view region,
      const envoy::extensions::common::aws::v3::AssumeRoleWithWebIdentityCredentialProvider&
          web_identity_config) const override;

  AwsClusterManagerPtr aws_cluster_manager_;
};

using InstanceProfileCredentialsProviderPtr = std::shared_ptr<InstanceProfileCredentialsProvider>;
using ContainerCredentialsProviderPtr = std::shared_ptr<ContainerCredentialsProvider>;
using WebIdentityCredentialsProviderPtr = std::shared_ptr<WebIdentityCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
