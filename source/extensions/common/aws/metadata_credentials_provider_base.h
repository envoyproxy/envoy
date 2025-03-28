#pragma once

#include "source/extensions/common/aws/aws_cluster_manager.h"
#include "source/extensions/common/aws/cached_credentials_provider_base.h"
#include "source/extensions/common/aws/credentials_provider.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr std::chrono::seconds REFRESH_GRACE_PERIOD{5};
constexpr char ACCESS_KEY_ID[] = "AccessKeyId";
constexpr char SECRET_ACCESS_KEY[] = "SecretAccessKey";
constexpr char TOKEN[] = "Token";

#define ALL_METADATACREDENTIALSPROVIDER_STATS(COUNTER, GAUGE)                                      \
  COUNTER(credential_refreshes_performed)                                                          \
  COUNTER(credential_refreshes_failed)                                                             \
  COUNTER(credential_refreshes_succeeded)                                                          \
  GAUGE(metadata_refresh_state, Accumulate)

struct MetadataCredentialsProviderStats {
  ALL_METADATACREDENTIALSPROVIDER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 *  CreateMetadataFetcherCb is a callback interface for creating a MetadataFetcher instance.
 */
using CreateMetadataFetcherCb =
    std::function<MetadataFetcherPtr(Upstream::ClusterManager&, absl::string_view)>;
using ServerFactoryContextOptRef = OptRef<Server::Configuration::ServerFactoryContext>;

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
  bool credentialsPending() override;

  // Get the Metadata credentials cache duration.
  static std::chrono::seconds getCacheDuration();

  // Store the RAII cluster callback handle following registration call with AWS cluster manager
  void setClusterReadyCallbackHandle(AwsManagedClusterUpdateCallbacksHandlePtr handle) {
    callback_handle_ = std::move(handle);
  }

  CredentialSubscriberCallbacksHandlePtr
  subscribeToCredentialUpdates(CredentialSubscriberCallbacks& cs);

protected:
  struct ThreadLocalCredentialsCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCredentialsCache() : credentials_(std::make_shared<Credentials>()) {};

    // The credentials object.
    CredentialsConstSharedPtr credentials_;
    // Lock guard.
    Thread::MutexBasicLockable lock_;
  };

  // Set anonymous credentials to all threads, update stats and close async
  void credentialsRetrievalError();

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
  // Are credentials pending?
  std::atomic<bool> credentials_pending_ = true;
  Thread::MutexBasicLockable mu_;
  std::list<CredentialSubscriberCallbacks*> credentials_subscribers_ ABSL_GUARDED_BY(mu_) = {};
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
