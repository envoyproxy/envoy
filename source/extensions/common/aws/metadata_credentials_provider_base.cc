#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// TODO(suniltheta): The field context is of type ServerFactoryContextOptRef so
// that an optional empty value can be set. Especially in aws iam plugin the cluster manager
// obtained from server factory context object is not fully initialized due to the
// reasons explained in https://github.com/envoyproxy/envoy/issues/27586 which cannot
// utilize http async client here to fetch AWS credentials. For time being if context
// is empty then will use libcurl to fetch the credentials.

MetadataCredentialsProviderBase::MetadataCredentialsProviderBase(
    Api::Api& api, ServerFactoryContextOptRef context, AwsClusterManagerOptRef aws_cluster_manager,
    absl::string_view cluster_name, const CurlMetadataFetcher& fetch_metadata_using_curl,
    CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer)
    : api_(api), context_(context), fetch_metadata_using_curl_(fetch_metadata_using_curl),
      create_metadata_fetcher_cb_(create_metadata_fetcher_cb), cluster_name_(cluster_name),
      cache_duration_(getCacheDuration()), refresh_state_(refresh_state),
      initialization_timer_(initialization_timer), aws_cluster_manager_(aws_cluster_manager) {

  // Most code sets the context and uses the async http client, except for one extension
  // which is scheduled to be deprecated and deleted. Modes can no longer be switched via runtime,
  // so each caller should only pass parameters to support a single mode.
  // https://github.com/envoyproxy/envoy/issues/36910
  ASSERT((context.has_value() ^ (fetch_metadata_using_curl != nullptr)));

  // Async provider cluster setup
  if (context_) {
    // Set up metadata credentials statistics
    scope_ = api.rootScope().createScope(
        fmt::format("aws.metadata_credentials_provider.{}.", cluster_name_));
    stats_ = std::make_shared<MetadataCredentialsProviderStats>(MetadataCredentialsProviderStats{
        ALL_METADATACREDENTIALSPROVIDER_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))});
    stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));

    tls_slot_ =
        ThreadLocal::TypedSlot<ThreadLocalCredentialsCache>::makeUnique(context_->threadLocal());

    tls_slot_->set(
        [&](Event::Dispatcher&) { return std::make_shared<ThreadLocalCredentialsCache>(); });
  }
};

void MetadataCredentialsProviderBase::onClusterAddOrUpdate() {
  ENVOY_LOG(debug, "Received callback from aws cluster manager for cluster {}", cluster_name_);
  if (!cache_duration_timer_) {
    cache_duration_timer_ = context_->mainThreadDispatcher().createTimer([this]() -> void {
      stats_->credential_refreshes_performed_.inc();
      refresh();
    });
  }
  if (!cache_duration_timer_->enabled()) {
    cache_duration_timer_->enableTimer(std::chrono::milliseconds(1));
  }
}

void MetadataCredentialsProviderBase::credentialsRetrievalError() {
  // Credential retrieval failed, so set blank (anonymous) credentials
  if (context_) {
    stats_->credential_refreshes_failed_.inc();
    ENVOY_LOG(debug, "Error retrieving credentials, settings anonymous credentials");
    setCredentialsToAllThreads(std::make_unique<Credentials>());
    handleFetchDone();
  }
}

// Async provider uses its own refresh mechanism. Calling refreshIfNeeded() here is not thread safe.
bool MetadataCredentialsProviderBase::credentialsPending() {
  if (context_) {
    return credentials_pending_;
  }
  return false;
}

// Async provider uses its own refresh mechanism. Calling refreshIfNeeded() here is not thread safe.
Credentials MetadataCredentialsProviderBase::getCredentials() {

  if (context_) {
    if (tls_slot_) {
      return *(*tls_slot_)->credentials_.get();
    } else {
      return Credentials();
    }

  } else {
    // Refresh for non async case
    refreshIfNeeded();
    return cached_credentials_;
  }
}

std::chrono::seconds MetadataCredentialsProviderBase::getCacheDuration() {
  return std::chrono::seconds(
      REFRESH_INTERVAL -
      REFRESH_GRACE_PERIOD /*TODO: Add jitter from context.api().randomGenerator()*/);
}

void MetadataCredentialsProviderBase::handleFetchDone() {
  if (context_) {
    if (cache_duration_timer_ && !cache_duration_timer_->enabled()) {
      // Receiver state handles the initial credential refresh scenario. If for some reason we are
      // unable to perform credential refresh after cluster initialization has completed, we use a
      // short timer to keep retrying. Once successful, we fall back to the normal cache duration
      // or whatever expiration is provided in the credential payload
      if (refresh_state_ == MetadataFetcher::MetadataReceiver::RefreshState::FirstRefresh) {
        cache_duration_timer_->enableTimer(initialization_timer_);
        ENVOY_LOG(debug, "Metadata fetcher initialization failed, retrying in {}",
                  std::chrono::seconds(initialization_timer_.count()));
        // Timer begins at 2 seconds and doubles each time, to a maximum of 32 seconds. This avoids
        // excessive retries against STS or instance metadata service
        if (initialization_timer_ < std::chrono::seconds(32)) {
          initialization_timer_ = initialization_timer_ * 2;
        }
      } else {
        // If our returned token had an expiration time, use that to set the cache duration
        const auto now = api_.timeSource().systemTime();
        if (expiration_time_.has_value() && (expiration_time_.value() > now)) {
          cache_duration_ =
              std::chrono::duration_cast<std::chrono::seconds>(expiration_time_.value() - now);
          ENVOY_LOG(debug,
                    "Metadata fetcher setting credential refresh to {}, based on "
                    "credential expiration",
                    std::chrono::seconds(cache_duration_.count()));
        } else {
          cache_duration_ = getCacheDuration();
          ENVOY_LOG(
              debug,
              "Metadata fetcher setting credential refresh to {}, based on default expiration",
              std::chrono::seconds(cache_duration_.count()));
        }
        cache_duration_timer_->enableTimer(cache_duration_);
      }
    }
  }
}

void MetadataCredentialsProviderBase::setCredentialsToAllThreads(
    CredentialsConstUniquePtr&& creds) {

  ENVOY_LOG(debug, "{}: Setting credentials to all threads", this->providerName());

  CredentialsConstSharedPtr shared_credentials = std::move(creds);
  if (tls_slot_ && !tls_slot_->isShutdown()) {
    tls_slot_->runOnAllThreads(
        /* Set the credentials */ [shared_credentials](
                                      OptRef<ThreadLocalCredentialsCache>
                                          obj) { obj->credentials_ = shared_credentials; },
        /* Notify waiting signers on completion of credential setting above */
        [this]() {
          credentials_pending_.store(false);
          std::list<CredentialSubscriberCallbacks*> subscribers_copy;
          {
            Thread::LockGuard guard(mu_);
            subscribers_copy = credentials_subscribers_;
          }
          for (auto& cb : subscribers_copy) {
            ENVOY_LOG(debug, "Notifying subscriber of credential update");
            cb->onCredentialUpdate();
          }
        });
  }
}

CredentialSubscriberCallbacksHandlePtr
MetadataCredentialsProviderBase::subscribeToCredentialUpdates(CredentialSubscriberCallbacks& cs) {
  Thread::LockGuard guard(mu_);
  return std::make_unique<CredentialSubscriberCallbacksHandle>(cs, credentials_subscribers_);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
