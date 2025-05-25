#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

MetadataCredentialsProviderBase::MetadataCredentialsProviderBase(
    Server::Configuration::ServerFactoryContext& context, AwsClusterManagerPtr aws_cluster_manager,
    absl::string_view cluster_name, CreateMetadataFetcherCb create_metadata_fetcher_cb,
    MetadataFetcher::MetadataReceiver::RefreshState refresh_state,
    std::chrono::seconds initialization_timer)
    : context_(context), create_metadata_fetcher_cb_(create_metadata_fetcher_cb),
      cluster_name_(cluster_name), cache_duration_(getCacheDuration()),
      refresh_state_(refresh_state), initialization_timer_(initialization_timer),
      aws_cluster_manager_(aws_cluster_manager) {

  // Set up metadata credentials statistics
  scope_ = context_.api().rootScope().createScope(
      fmt::format("aws.metadata_credentials_provider.{}.", cluster_name_));
  stats_ = std::make_shared<MetadataCredentialsProviderStats>(MetadataCredentialsProviderStats{
      ALL_METADATACREDENTIALSPROVIDER_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))});
  stats_->metadata_refresh_state_.set(uint64_t(refresh_state_));

  tls_slot_ =
      ThreadLocal::TypedSlot<ThreadLocalCredentialsCache>::makeUnique(context_.threadLocal());

  tls_slot_->set(
      [&](Event::Dispatcher&) { return std::make_shared<ThreadLocalCredentialsCache>(); });
};

void MetadataCredentialsProviderBase::onClusterAddOrUpdate() {
  ENVOY_LOG(debug, "Received callback from aws cluster manager for cluster {}", cluster_name_);
  if (!cache_duration_timer_) {
    cache_duration_timer_ = context_.mainThreadDispatcher().createTimer([this]() -> void {
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
  stats_->credential_refreshes_failed_.inc();
  ENVOY_LOG(debug, "Error retrieving credentials, settings anonymous credentials");
  setCredentialsToAllThreads(std::make_unique<Credentials>());
  handleFetchDone();
}

bool MetadataCredentialsProviderBase::credentialsPending() { return credentials_pending_; }

Credentials MetadataCredentialsProviderBase::getCredentials() {
  return *(*tls_slot_)->credentials_.get();
}

std::chrono::seconds MetadataCredentialsProviderBase::getCacheDuration() {
  return std::chrono::seconds(
      REFRESH_INTERVAL -
      REFRESH_GRACE_PERIOD /*TODO: Add jitter from context.api().randomGenerator()*/);
}

void MetadataCredentialsProviderBase::handleFetchDone() {
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
      const auto now = context_.api().timeSource().systemTime();
      if (expiration_time_.has_value() && (expiration_time_.value() > now)) {
        cache_duration_ =
            std::chrono::duration_cast<std::chrono::seconds>(expiration_time_.value() - now);
        ENVOY_LOG(debug,
                  "Metadata fetcher setting credential refresh to {}, based on "
                  "credential expiration",
                  std::chrono::seconds(cache_duration_.count()));
      } else {
        cache_duration_ = getCacheDuration();
        ENVOY_LOG(debug,
                  "Metadata fetcher setting credential refresh to {}, based on default expiration",
                  std::chrono::seconds(cache_duration_.count()));
      }
      cache_duration_timer_->enableTimer(cache_duration_);
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
