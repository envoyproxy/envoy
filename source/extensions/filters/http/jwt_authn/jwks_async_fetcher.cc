#include "source/extensions/filters/http/jwt_authn/jwks_async_fetcher.h"

#include "source/common/protobuf/utility.h"
#include "source/common/tracing/http_tracer_impl.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwksAsyncFetch;
using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// Default cache expiration time in 10 minutes.
constexpr std::chrono::seconds DefaultCacheExpirationSec{600};

// Number of seconds to refetch before a cached jwks is expired.
constexpr std::chrono::seconds RefetchBeforeExpiredSec{5};

// Default number of seconds to refetch after a failed fetch.
constexpr std::chrono::seconds DefaultRefetchAfterFailedSec{1};

std::chrono::milliseconds getFailedRefetchDuration(const JwksAsyncFetch& async_fetch) {
  if (async_fetch.has_failed_refetch_duration()) {
    // Use `durationToMilliseconds` as it has stricter max boundary to the `seconds` value to avoid
    // overflow.
    return std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(async_fetch.failed_refetch_duration()));
  }
  return DefaultRefetchAfterFailedSec;
}

} // namespace

JwksAsyncFetcher::JwksAsyncFetcher(const RemoteJwks& remote_jwks,
                                   Server::Configuration::FactoryContext& context,
                                   CreateJwksFetcherCb create_fetcher_fn,
                                   JwtAuthnFilterStats& stats, JwksDoneFetched done_fn)
    : remote_jwks_(remote_jwks), context_(context), create_fetcher_fn_(create_fetcher_fn),
      stats_(stats), done_fn_(done_fn),
      debug_name_(absl::StrCat("Jwks async fetching url=", remote_jwks_.http_uri().uri())) {
  // if async_fetch is not enabled, do nothing.
  if (!remote_jwks_.has_async_fetch()) {
    return;
  }

  // Next refetch time should be: jwks_cache_duration - RefetchBeforeExpiredSec.
  // Using RefetchBeforeExpiredSec is to avoid on_demand fetch.
  // During the authentication, if jwks is not fetched or is expired, it will trigger
  // an on-demand fetch. But async_fetch is preferred as it is done in the main thread
  // and not need to block request processing but on-demand fetch is done in the worker
  // thread and block request processing.
  good_refetch_duration_ = getCacheDuration(remote_jwks);
  if (good_refetch_duration_ > RefetchBeforeExpiredSec) {
    good_refetch_duration_ = good_refetch_duration_ - RefetchBeforeExpiredSec;
  }
  failed_refetch_duration_ = getFailedRefetchDuration(remote_jwks.async_fetch());

  refetch_timer_ = context_.serverFactoryContext().mainThreadDispatcher().createTimer(
      [this]() -> void { fetch(); });

  // For fast_listener, just trigger a fetch, not register with init_manager.
  if (remote_jwks_.async_fetch().fast_listener()) {
    fetch();
    return;
  }

  // Register to init_manager, force the listener to wait for the fetching.
  init_target_ = std::make_unique<Init::TargetImpl>(debug_name_, [this]() -> void { fetch(); });
  context_.initManager().add(*init_target_);
}

std::chrono::seconds JwksAsyncFetcher::getCacheDuration(const RemoteJwks& remote_jwks) {
  if (remote_jwks.has_cache_duration()) {
    return std::chrono::seconds(DurationUtil::durationToSeconds(remote_jwks.cache_duration()));
  }
  return DefaultCacheExpirationSec;
}

void JwksAsyncFetcher::fetch() {
  if (fetcher_) {
    fetcher_->cancel();
  }

  ENVOY_LOG(debug, "{}: started", debug_name_);
  fetcher_ = create_fetcher_fn_(context_.serverFactoryContext().clusterManager(), remote_jwks_);
  fetcher_->fetch(Tracing::NullSpan::instance(), *this);
}

void JwksAsyncFetcher::handleFetchDone() {
  if (init_target_) {
    init_target_->ready();
    init_target_.reset();
  }
}

void JwksAsyncFetcher::onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) {
  done_fn_(std::move(jwks));
  handleFetchDone();
  refetch_timer_->enableTimer(good_refetch_duration_);
  stats_.jwks_fetch_success_.inc();

  // Note: not to free fetcher_ within onJwksSuccess or onJwksError function.
  // They are passed to fetcher_->fetch() and are called by fetcher_ after fetch is done.
  // After calling these callback functions, fetch_ calls its reset() function.
  // If fetcher_ is freed by the callback,  calling reset() will crash.

  // Not need to free fetcher_. At the next fetch(), it will be freed with a cancel() call.
  // The cancel() is needed to cancel the old call before the new one is created.
  // But it is a no-op if the call is completed.
}

void JwksAsyncFetcher::onJwksError(Failure) {
  ENVOY_LOG(warn, "{}: failed", debug_name_);
  handleFetchDone();
  refetch_timer_->enableTimer(failed_refetch_duration_);
  stats_.jwks_fetch_failed_.inc();

  // Note: not to free fetcher_ in this function. Please see comment at onJwksSuccess.
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
