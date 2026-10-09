#include "source/extensions/common/aws/metadata_credentials_provider_base.h"

#include <chrono>

#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

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

MetadataCredentialsProviderBase::~MetadataCredentialsProviderBase() {
  if (metadata_fetcher_) {
    metadata_fetcher_->cancel();
  }
}

void MetadataCredentialsProviderBase::onClusterAddOrUpdate() {
  ENVOY_LOG(debug, "Received callback from aws cluster manager for cluster {}", cluster_name_);
  if (!cache_duration_timer_) {
    std::weak_ptr<MetadataCredentialsProviderStats> weak_stats = stats_;
    std::weak_ptr<MetadataCredentialsProviderBase> weak_self = shared_from_this();
    cache_duration_timer_ =
        context_.mainThreadDispatcher().createTimer([weak_stats, weak_self]() -> void {
          if (auto stats = weak_stats.lock()) {
            stats->credential_refreshes_performed_.inc();
          }
          if (auto self = weak_self.lock()) {
            self->refresh();
          }
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

bool MetadataCredentialsProviderBase::credentialsPending() {
  if (!tls_slot_->currentThreadRegistered()) {
    ASSERT(false, "AWS credentials provider queried from a thread with no thread local storage");
    return true;
  }
  auto cache = tls_slot_->get();
  ASSERT(cache.has_value());
  return !cache.has_value() || cache->credentials_pending_;
}

Credentials MetadataCredentialsProviderBase::getCredentials() {
  return *(*tls_slot_)->credentials_.get();
}

// getCacheDuration will return a duration between 3566 and 3595 seconds, IE close to 1 hour with
// jitter.
std::chrono::seconds MetadataCredentialsProviderBase::getCacheDuration() {
  const auto jitter =
      std::chrono::seconds(context_.api().randomGenerator().random() % MAX_CACHE_JITTER.count());
  return std::chrono::seconds(REFRESH_INTERVAL - REFRESH_GRACE_PERIOD - jitter);
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
        auto time_until_expiration = expiration_time_.value() - now;
        auto grace_period =
            std::chrono::duration_cast<std::chrono::system_clock::duration>(REFRESH_GRACE_PERIOD);

        // Subtract grace period, but ensure we don't go negative
        if (time_until_expiration > grace_period) {
          cache_duration_ = std::chrono::duration_cast<std::chrono::seconds>(time_until_expiration -
                                                                             grace_period);
        } else {
          ENVOY_LOG(warn,
                    "Credential expiration time is within grace period {} seconds, refreshing now. "
                    "Minimum expiration time should be 900 seconds (15 minutes).",
                    REFRESH_GRACE_PERIOD.count());
          cache_duration_ = std::chrono::seconds(1);
        }

        ENVOY_LOG(debug,
                  "Metadata fetcher setting credential refresh to {}, based on "
                  "credential expiration with grace period",
                  std::chrono::seconds(cache_duration_.count()));
      } else {
        cache_duration_ = getCacheDuration();
        ENVOY_LOG(debug,
                  "Metadata fetcher setting credential refresh to {}, based on default expiration",
                  std::chrono::seconds(cache_duration_.count()));
      }
      cache_duration_timer_->enableTimer(
          std::chrono::duration_cast<std::chrono::milliseconds>(cache_duration_));
    }
  }
}

void MetadataCredentialsProviderBase::setCredentialsToAllThreads(
    CredentialsConstUniquePtr&& creds) {

  ENVOY_LOG(debug, "{}: Setting credentials to all threads", this->providerName());

  CredentialsConstSharedPtr shared_credentials = std::move(creds);
  if (tls_slot_ && !tls_slot_->isShutdown()) {
    // A weak_ptr rather than a raw `this`, so that a completion callback still queued when the
    // provider goes away becomes a no-op instead of a use-after-free.
    std::weak_ptr<MetadataCredentialsProviderBase> weak_self = weak_from_this();

    // Set the credentials and clear the pending flag as a single update, so that no thread can
    // observe one without the other. This writes the main thread's slot synchronously and posts the
    // same update to every registered worker dispatcher.
    tls_slot_->runOnAllThreads(
        [shared_credentials](OptRef<ThreadLocalCredentialsCache> obj) {
          obj->credentials_ = shared_credentials;
          obj->credentials_pending_ = false;
        },
        // Notify a second time once every worker has applied the update. Between the immediate
        // notification below and a worker applying its update, that worker still reads
        // `credentials_pending_ == true` from its own slot, so it can queue a pending callback
        // after the immediate notification has already drained the queue. This notification is
        // what wakes such a callback; without it the request stalls until the next successful
        // refresh.
        [weak_self]() {
          if (auto self = weak_self.lock()) {
            self->notifySubscribers();
          }
        });

    // Notify waiting signers from this thread as well, rather than relying only on the
    // all-threads-complete callback above. That callback does not run until every registered worker
    // dispatcher has handled the posted update, and worker dispatchers do not start running until
    // `startWorkers()`, which waits on server initialization. The main thread might like to use
    // credentials before that point, and waiting for the workers would deadlock. (For example, a
    // dynamic modules bootstrap extension might want to make an HTTP callout before it signals
    // server init is complete.)
    //
    // For subscribers that post their wakeup to their own dispatcher (the AWS request signing and
    // Lambda filters do), notifying here is ordered correctly: the credential update above is
    // posted to each worker first, and dispatcher post queues are FIFO, so a worker applies the
    // update before it runs the wakeup that reads it. Subscribers that instead run their callback
    // inline rely on this being the main thread, whose slot `runOnAllThreads` has already updated.
    //
    // Note that unlike the completion callback, this notification runs on the caller's stack, which
    // for a credential refresh is inside MetadataFetcher::onSuccess()/onMetadataError() and ahead
    // of handleFetchDone(). No subscriber re-enters the provider today, but one that did would see
    // a half-finished refresh.
    notifySubscribers();
  }
}

void MetadataCredentialsProviderBase::notifySubscribers() {
  std::list<std::weak_ptr<CredentialSubscriberCallbacks>> subscribers_copy;
  {
    Thread::LockGuard guard(mu_);
    subscribers_copy = credentials_subscribers_;
  }
  for (auto& weak_cb : subscribers_copy) {
    if (auto cb = weak_cb.lock()) {
      ENVOY_LOG(debug, "Notifying subscriber of credential update");
      cb->onCredentialUpdate();
    }
  }
}

void MetadataCredentialsProviderBase::setCredentialsPendingToAllThreads() {
  // The dedup below reads the main thread's slot, so it is only meaningful on the main thread. Not
  // relying on the assertion inside runOnAllThreads(), because the dedup can return before ever
  // reaching it.
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!tls_slot_ || tls_slot_->isShutdown()) {
    return;
  }
  // The main thread's slot is written synchronously by runOnAllThreads, so it always holds the most
  // recently initiated update. If it already says pending then every other thread has the same
  // update applied or queued, and there is nothing to broadcast.
  if ((*tls_slot_)->credentials_pending_) {
    return;
  }
  tls_slot_->runOnAllThreads(
      [](OptRef<ThreadLocalCredentialsCache> obj) { obj->credentials_pending_ = true; });
}

CredentialSubscriberCallbacksHandlePtr
MetadataCredentialsProviderBase::subscribeToCredentialUpdates(
    CredentialSubscriberCallbacksSharedPtr cs) {
  Thread::LockGuard guard(mu_);
  return std::make_unique<CredentialSubscriberCallbacksHandle>(cs, credentials_subscribers_);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
