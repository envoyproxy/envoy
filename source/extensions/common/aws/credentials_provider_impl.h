#pragma once

#include <list>
#include <memory>
#include <optional>
#include <string>

#include "envoy/api/api.h"
#include "envoy/common/optref.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"
#include "envoy/http/message.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/init/target_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
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
  CredentialsFileCredentialsProvider(Api::Api& api) : CredentialsFileCredentialsProvider(api, "") {}

  CredentialsFileCredentialsProvider(Api::Api& api, const std::string& profile)
      : api_(api), profile_(profile) {}

private:
  Api::Api& api_;
  const std::string profile_;

  bool needsRefresh() override;
  void refresh() override;
  void extractCredentials(const std::string& credentials_file, const std::string& profile);
};

class LoadClusterEntryHandle {
public:
  virtual ~LoadClusterEntryHandle() = default;
};

#define ALL_METADATACREDENTIALSPROVIDER_STATS(COUNTER, GAUGE)                                      \
  COUNTER(credential_refreshes_performed)                                                          \
  COUNTER(credential_refreshes_failed)                                                             \
  COUNTER(credential_refreshes_succeeded)                                                          \
  COUNTER(clusters_removed_by_cds)                                                                 \
  COUNTER(clusters_readded_after_cds)                                                              \
  GAUGE(metadata_refresh_state, Accumulate)

struct MetadataCredentialsProviderStats {
  ALL_METADATACREDENTIALSPROVIDER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

using LoadClusterEntryHandlePtr = std::unique_ptr<LoadClusterEntryHandle>;

class MetadataCredentialsProviderBase : public CachedCredentialsProviderBase {
public:
  using CurlMetadataFetcher = std::function<absl::optional<std::string>(Http::RequestMessage&)>;
  using OnAsyncFetchCb = std::function<void(const std::string&&)>;

  MetadataCredentialsProviderBase(
      Api::Api& api, ServerFactoryContextOptRef context,
      const CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type, absl::string_view uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer);

  Credentials getCredentials() override;

  // Get the Metadata credentials cache duration.
  static std::chrono::seconds getCacheDuration();

private:
  void createCluster(bool new_timer);

protected:
  struct LoadClusterEntryHandleImpl
      : public LoadClusterEntryHandle,
        RaiiMapOfListElement<std::string, LoadClusterEntryHandleImpl*> {
    LoadClusterEntryHandleImpl(
        absl::flat_hash_map<std::string, std::list<LoadClusterEntryHandleImpl*>>& parent,
        absl::string_view host, Envoy::Event::TimerPtr& timer)
        : RaiiMapOfListElement<std::string, LoadClusterEntryHandleImpl*>(parent, host, this),
          timer_(timer) {}

    Envoy::Event::TimerPtr& timer_;
  };

  struct ThreadLocalCredentialsCache : public ThreadLocal::ThreadLocalObject,
                                       public Upstream::ClusterUpdateCallbacks {
    ThreadLocalCredentialsCache(MetadataCredentialsProviderBase& parent)
        : handle_(parent.context_->clusterManager().addThreadLocalClusterUpdateCallbacks(*this)),
          parent_(parent), credentials_(std::make_shared<Credentials>()){};

    ~ThreadLocalCredentialsCache() override;

    Upstream::ClusterUpdateCallbacksHandlePtr handle_;
    // Parent credentials provider object
    MetadataCredentialsProviderBase& parent_;
    // The credentials object.
    CredentialsConstSharedPtr credentials_;
    absl::flat_hash_map<std::string, std::list<LoadClusterEntryHandleImpl*>> pending_clusters_;
    // Lock guard.
    Thread::MutexBasicLockable lock_;

  private:
    void onClusterAddOrUpdate(absl::string_view cluster_name,
                              Upstream::ThreadLocalClusterCommand&) override;
    void onClusterRemoval(const std::string&) override;
  };

  const std::string& clusterName() const { return cluster_name_; }

  // Handle fetch done.
  void handleFetchDone();

  // Set Credentials shared_ptr on all threads.
  void setCredentialsToAllThreads(CredentialsConstUniquePtr&& creds);

  // Returns true if http async client can be used instead of libcurl to fetch the aws credentials,
  // else false.
  bool useHttpAsyncClient();

  Api::Api& api_;
  // The optional server factory context.
  ServerFactoryContextOptRef context_;
  // Store the method to fetch metadata from libcurl (deprecated)
  CurlMetadataFetcher fetch_metadata_using_curl_;
  // The callback used to create a MetadataFetcher instance.
  CreateMetadataFetcherCb create_metadata_fetcher_cb_;
  // The cluster name to use for internal static cluster pointing towards the credentials provider.
  std::string cluster_name_;
  // The cluster type to use for internal static cluster pointing towards the credentials provider.
  const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type_;
  // The uri of internal static cluster credentials provider.
  const std::string uri_;
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
  // The init target.
  std::unique_ptr<Init::TargetImpl> init_target_;
  // Used in logs.
  const std::string debug_name_;
  // The expiration time received in any returned token
  absl::optional<SystemTime> expiration_time_;
  // Tls slot
  ThreadLocal::TypedSlotPtr<ThreadLocalCredentialsCache> tls_slot_ = nullptr;
  // Storage for our per cluster credential timers
  LoadClusterEntryHandlePtr cluster_load_handle_;
  // Stats scope
  Stats::ScopeSharedPtr scope_ = nullptr;
  // Pointer to our stats structure
  std::shared_ptr<MetadataCredentialsProviderStats> stats_;
  // Atomic flag for cluster recreate
  std::atomic<bool> is_creating_ = false;
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
  WebIdentityCredentialsProvider(Api::Api& api, ServerFactoryContextOptRef context,
                                 const CurlMetadataFetcher& fetch_metadata_using_curl,
                                 CreateMetadataFetcherCb create_metadata_fetcher_cb,
                                 absl::string_view token_file_path, absl::string_view token,
                                 absl::string_view sts_endpoint, absl::string_view role_arn,
                                 absl::string_view role_session_name,
                                 MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
                                 std::chrono::seconds initialization_timer,
                                 absl::string_view cluster_name);

  // Following functions are for MetadataFetcher::MetadataReceiver interface
  void onMetadataSuccess(const std::string&& body) override;
  void onMetadataError(Failure reason) override;

  const std::string& tokenForTesting() const { return token_; }
  const std::string& roleArnForTesting() const { return role_arn_; }

private:
  // token_ and token_file_path_ are mutually exclusive. If token_ is set, token_file_path_ is not
  // used.
  const std::string token_file_path_;
  const std::string token_;
  const std::string sts_endpoint_;
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

  virtual CredentialsProviderSharedPtr
  createCredentialsFileCredentialsProvider(Api::Api& api) const PURE;

  virtual CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view token_file_path, absl::string_view token, absl::string_view sts_endpoint,
      absl::string_view role_arn, absl::string_view role_session_name,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer) const PURE;

  virtual CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      absl::string_view authorization_token = {}) const PURE;

  virtual CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) const PURE;
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
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl)
      : DefaultCredentialsProviderChain(api, context, singleton_manager, region,
                                        fetch_metadata_using_curl, *this) {}

  DefaultCredentialsProviderChain(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      absl::string_view region,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      const CredentialsProviderChainFactories& factories);

private:
  CredentialsProviderSharedPtr createEnvironmentCredentialsProvider() const override {
    return std::make_shared<EnvironmentCredentialsProvider>();
  }

  CredentialsProviderSharedPtr
  createCredentialsFileCredentialsProvider(Api::Api& api) const override {
    return std::make_shared<CredentialsFileCredentialsProvider>(api);
  }

  CredentialsProviderSharedPtr createContainerCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view credential_uri,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer,
      absl::string_view authorization_token) const override;

  CredentialsProviderSharedPtr createInstanceProfileCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context, Singleton::Manager& singleton_manager,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer, absl::string_view cluster_name) const override;

  CredentialsProviderSharedPtr createWebIdentityCredentialsProvider(
      Api::Api& api, ServerFactoryContextOptRef context,
      const MetadataCredentialsProviderBase::CurlMetadataFetcher& fetch_metadata_using_curl,
      CreateMetadataFetcherCb create_metadata_fetcher_cb, absl::string_view cluster_name,
      absl::string_view token_file_path, absl::string_view token, absl::string_view sts_endpoint,
      absl::string_view role_arn, absl::string_view role_session_name,
      MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
      std::chrono::seconds initialization_timer) const override {
    return std::make_shared<WebIdentityCredentialsProvider>(
        api, context, fetch_metadata_using_curl, create_metadata_fetcher_cb, token_file_path, token,
        sts_endpoint, role_arn, role_session_name, refresh_state, initialization_timer,
        cluster_name);
  }
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
 * Create an AWS credentials provider from the proto configuration instead of using the default
 * credentials provider chain.
 */
absl::StatusOr<CredentialsProviderSharedPtr> createCredentialsProviderFromConfig(
    Server::Configuration::ServerFactoryContext& context, absl::string_view region,
    const envoy::extensions::common::aws::v3::AwsCredentialProvider& config);

using InstanceProfileCredentialsProviderPtr = std::shared_ptr<InstanceProfileCredentialsProvider>;
using ContainerCredentialsProviderPtr = std::shared_ptr<ContainerCredentialsProvider>;
using WebIdentityCredentialsProviderPtr = std::shared_ptr<WebIdentityCredentialsProvider>;

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
