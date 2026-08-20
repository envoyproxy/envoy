#include "source/extensions/filters/common/jwks/jwks_provider.h"

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/config/datasource.h"
#include "source/common/http/utility.h"
#include "source/common/jwt/status.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/extensions/filters/http/jwt_authn/jwks_async_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Jwks {
namespace {

using JwtAuthnRemoteJwks = envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
using CommonRemoteJwks = envoy::extensions::filters::common::jwks::v3::RemoteJwks;

// The background fetcher takes jwt_authn's RemoteJwks, not the common one, so translate.
// TODO(kanurag94): remove once the fetcher accepts the common RemoteJwks directly.
JwtAuthnRemoteJwks toJwtAuthnRemoteJwks(const CommonRemoteJwks& src) {
  JwtAuthnRemoteJwks dst;
  *dst.mutable_http_uri() = src.http_uri();
  if (src.has_cache_duration()) {
    *dst.mutable_cache_duration() = src.cache_duration();
  }
  if (src.has_async_fetch()) {
    dst.mutable_async_fetch()->set_fast_listener(src.async_fetch().fast_listener());
    if (src.async_fetch().has_failed_refetch_duration()) {
      *dst.mutable_async_fetch()->mutable_failed_refetch_duration() =
          src.async_fetch().failed_refetch_duration();
    }
  }
  if (src.has_retry_policy()) {
    *dst.mutable_retry_policy() = src.retry_policy();
  }
  return dst;
}

// Raises a cache_duration or failed_refetch_duration below one second up to one second. The fetcher
// waits this long between fetches, so a shorter value would refetch with no real pause. Returns an
// error only for an out-of-range duration.
// TODO(kanurag94): remove once the background fetcher floors these durations itself.
absl::Status clampRefetchDurationsToOneSecond(JwtAuthnRemoteJwks& remote_jwks) {
  const auto raise_to_one_second = [](Protobuf::Duration& duration) -> absl::Status {
    const auto ms = DurationUtil::durationToMillisecondsNoThrow(duration);
    if (!ms.ok()) {
      return ms.status();
    }
    if (ms.value() < 1000) {
      duration.set_seconds(1);
      duration.set_nanos(0);
    }
    return absl::OkStatus();
  };
  if (remote_jwks.has_cache_duration()) {
    if (absl::Status status = raise_to_one_second(*remote_jwks.mutable_cache_duration());
        !status.ok()) {
      return status;
    }
  }
  if (remote_jwks.async_fetch().has_failed_refetch_duration()) {
    if (absl::Status status = raise_to_one_second(
            *remote_jwks.mutable_async_fetch()->mutable_failed_refetch_duration());
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

// Fetches a remote JWKS in the background, at startup and every cache_duration, so verification
// stays synchronous. Holds its own copy of the keys for worker threads to read safely.
class RemoteJwksProvider : public JwksProvider {
public:
  RemoteJwksProvider(const JwtAuthnRemoteJwks& remote_jwks,
                     Router::RetryPolicyConstSharedPtr retry_policy,
                     Server::Configuration::FactoryContext& context,
                     JwksFetcherFactory create_fetcher_fn, Stats::Counter& fetch_success,
                     Stats::Counter& fetch_failed)
      : remote_jwks_(remote_jwks) {
    JwksFetcherFactory fetcher_cb =
        create_fetcher_fn
            ? std::move(create_fetcher_fn)
            : JwksFetcherFactory(&Extensions::HttpFilters::Common::JwksFetcher::create);
    async_fetcher_ = std::make_unique<Extensions::HttpFilters::JwtAuthn::JwksAsyncFetcher>(
        remote_jwks_, std::move(retry_policy), context, std::move(fetcher_cb), fetch_success,
        fetch_failed, [this](JwtVerify::JwksPtr&& jwks) {
          Thread::LockGuard lock(mutex_);
          jwks_ = std::move(jwks);
        });
  }

  JwksConstSharedPtr jwks() const override {
    Thread::LockGuard lock(mutex_);
    return jwks_;
  }

private:
  // Kept here because JwksAsyncFetcher holds it by const reference and must outlive the fetcher.
  const JwtAuthnRemoteJwks remote_jwks_;
  // Guards the fetched keys. Workers read them while the fetcher callback replaces them.
  mutable Thread::MutexBasicLockable mutex_;
  JwksConstSharedPtr jwks_ ABSL_GUARDED_BY(mutex_);
  // Declared last so it is destroyed first. Its callback captures this, so the fetcher must stop
  // before the members it touches are gone.
  Extensions::HttpFilters::JwtAuthn::JwksAsyncFetcherPtr async_fetcher_;
};

} // namespace

absl::StatusOr<JwksProviderSharedPtr>
createStaticJwksProvider(const envoy::config::core::v3::DataSource& data_source, Api::Api& api) {
  auto jwks_or_error = Config::DataSource::read(data_source, /*allow_empty=*/false, api);
  if (!jwks_or_error.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("failed to load local_jwks: {}", jwks_or_error.status().message()));
  }
  auto jwks = JwtVerify::Jwks::createFrom(jwks_or_error.value(), JwtVerify::Jwks::JWKS);
  if (jwks->getStatus() != JwtVerify::Status::Ok) {
    return absl::InvalidArgumentError(
        fmt::format("invalid local_jwks: {}", JwtVerify::getStatusString(jwks->getStatus())));
  }
  return std::make_shared<StaticJwksProvider>(std::move(jwks));
}

absl::StatusOr<JwksProviderSharedPtr>
createRemoteJwksProvider(const CommonRemoteJwks& common_remote_jwks,
                         Server::Configuration::FactoryContext& context,
                         JwksFetcherFactory create_fetcher_fn, Stats::Counter& fetch_success,
                         Stats::Counter& fetch_failed) {
  JwtAuthnRemoteJwks remote_jwks = toJwtAuthnRemoteJwks(common_remote_jwks);
  Http::Utility::Url url;
  if (!url.initialize(remote_jwks.http_uri().uri(), /*is_connect_request=*/false)) {
    return absl::InvalidArgumentError(
        fmt::format("invalid remote_jwks http_uri: '{}'", remote_jwks.http_uri().uri()));
  }
  Router::RetryPolicyConstSharedPtr retry_policy;
  if (remote_jwks.has_retry_policy()) {
    auto retry_policy_or_error =
        buildRetryPolicy(remote_jwks.retry_policy(), context.serverFactoryContext());
    if (!retry_policy_or_error.ok()) {
      return absl::InvalidArgumentError(fmt::format("invalid remote_jwks retry_policy: {}",
                                                    retry_policy_or_error.status().message()));
    }
    retry_policy = std::move(retry_policy_or_error.value());
  }
  // This provider only fetches in the background, so turn async_fetch on. Without it the fetcher
  // would do nothing and no keys would ever load.
  if (!remote_jwks.has_async_fetch()) {
    remote_jwks.mutable_async_fetch();
  }
  if (absl::Status status = clampRefetchDurationsToOneSecond(remote_jwks); !status.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("invalid remote_jwks duration: {}", status.message()));
  }
  return std::make_shared<RemoteJwksProvider>(remote_jwks, std::move(retry_policy), context,
                                              std::move(create_fetcher_fn), fetch_success,
                                              fetch_failed);
}

absl::StatusOr<Router::RetryPolicyConstSharedPtr>
buildRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy,
                 Server::Configuration::ServerFactoryContext& server_context) {
  if (absl::Status status = Http::Utility::validateCoreRetryPolicy(retry_policy); !status.ok()) {
    return status;
  }
  const envoy::config::route::v3::RetryPolicy route_retry_policy =
      Http::Utility::convertCoreToRouteRetryPolicy(retry_policy,
                                                   "5xx,gateway-error,connect-failure,reset");
  auto policy_or_error = Router::RetryPolicyImpl::create(
      route_retry_policy, ProtobufMessage::getNullValidationVisitor(), server_context);
  if (!policy_or_error.ok()) {
    return policy_or_error.status();
  }
  return Router::RetryPolicyConstSharedPtr{std::move(policy_or_error.value())};
}

} // namespace Jwks
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
