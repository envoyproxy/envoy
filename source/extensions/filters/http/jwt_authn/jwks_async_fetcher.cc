#include "extensions/filters/http/jwt_authn/jwks_async_fetcher.h"

#include "common/protobuf/utility.h"
#include "common/tracing/http_tracer_impl.h"

using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// Default cache expiration time in 5 minutes.
constexpr int PubkeyCacheExpirationSec = 600;

} // namespace

JwksAsyncFetcher::JwksAsyncFetcher(const RemoteJwks& remote_jwks,
                                   Server::Configuration::FactoryContext& context,
                                   CreateJwksFetcherCb create_fetcher_fn,
                                   JwtAuthnFilterStats& stats, JwksDoneFetched done_fn)
    : remote_jwks_(remote_jwks), context_(context), create_fetcher_fn_(create_fetcher_fn),
      stats_(stats), done_fn_(done_fn), cache_duration_(getCacheDuration(remote_jwks)),
      debug_name_(absl::StrCat("Jwks async fetching url=", remote_jwks_.http_uri().uri())) {
  // if async_fetch is not enabled, do nothing.
  if (!remote_jwks_.has_async_fetch() || !remote_jwks_.async_fetch().enable()) {
    return;
  }

  cache_duration_timer_ = context_.dispatcher().createTimer([this]() -> void { fetch(); });

  // For fast_listener, just trigger a fetch, not register with init_manager.
  if (remote_jwks_.async_fetch().fast_listener()) {
    fetch();
    return;
  }

  // Register to init_manager, force the listener to wait for the fetching.
  init_target_ = std::make_unique<Init::TargetImpl>(debug_name_, [this] { fetch(); });
  context_.initManager().add(*init_target_);
}

JwksAsyncFetcher::~JwksAsyncFetcher() {
  if (fetcher_) {
    fetcher_->cancel();
  }
}

std::chrono::seconds JwksAsyncFetcher::getCacheDuration(const RemoteJwks& remote_jwks) {
  if (remote_jwks.has_cache_duration()) {
    return std::chrono::seconds(DurationUtil::durationToSeconds(remote_jwks.cache_duration()));
  }
  return std::chrono::seconds(PubkeyCacheExpirationSec);
}

void JwksAsyncFetcher::fetch() {
  if (fetcher_) {
    fetcher_->cancel();
  }

  ENVOY_LOG(debug, "{}: started", debug_name_);
  fetcher_ = create_fetcher_fn_(context_.clusterManager());
  fetcher_->fetch(remote_jwks_.http_uri(), Tracing::NullSpan::instance(), *this);
}

void JwksAsyncFetcher::handleFetchDone() {
  if (init_target_) {
    init_target_->ready();
    init_target_.reset();
  }

  fail_retry_count_ = 0;
  cache_duration_timer_->enableTimer(cache_duration_);
}

void JwksAsyncFetcher::onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) {
  fetcher_.reset();
  stats_.jwks_fetch_success_.inc();

  done_fn_(std::move(jwks));
  handleFetchDone();
}

void JwksAsyncFetcher::onJwksError(Failure reason) {
  fetcher_.reset();
  stats_.jwks_fetch_failed_.inc();

  ENVOY_LOG(warn, "{}: failed", debug_name_);
  if (reason == Common::JwksFetcher::JwksReceiver::Failure::Network) {
    if (++fail_retry_count_ <= remote_jwks_.async_fetch().network_fail_retries()) {
      fetch();
      return;
    }
  }

  handleFetchDone();
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
