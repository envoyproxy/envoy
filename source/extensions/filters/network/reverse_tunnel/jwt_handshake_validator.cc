#include "source/extensions/filters/network/reverse_tunnel/jwt_handshake_validator.h"

#include <vector>

#include "envoy/extensions/filters/common/jwks/v3/jwks.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/config/datasource.h"
#include "source/common/http/utility.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"
#include "source/common/jwt/verify.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/extensions/filters/http/jwt_authn/jwks_async_fetcher.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

using RemoteJwks = envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
using CommonRemoteJwks = envoy::extensions::filters::common::jwks::v3::RemoteJwks;
using JwtHandshakeValidationProto =
    envoy::extensions::filters::network::reverse_tunnel::v3::JwtHandshakeValidation;

// The background JWKS fetcher takes jwt_authn's RemoteJwks, not the common one, so translate.
// TODO(kanurag94): remove once the fetcher accepts the common RemoteJwks directly.
RemoteJwks toJwtAuthnRemoteJwks(const CommonRemoteJwks& src) {
  RemoteJwks dst;
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

// Counters for the remote JWKS background fetcher. They live on the config and are updated on the
// main thread, unlike the per-connection handshake counters in ReverseTunnelStats.
#define ALL_REVERSE_TUNNEL_JWKS_STATS(COUNTER)                                                     \
  COUNTER(jwt_jwks_fetch_success)                                                                  \
  COUNTER(jwt_jwks_fetch_failed)

struct ReverseTunnelJwksStats {
  ALL_REVERSE_TUNNEL_JWKS_STATS(GENERATE_COUNTER_STRUCT)
};

// Builds the retry policy for a remote_jwks fetch, or a null policy when none is configured.
absl::StatusOr<Router::RetryPolicyConstSharedPtr>
buildRemoteJwksRetryPolicy(const RemoteJwks& remote_jwks,
                           Server::Configuration::ServerFactoryContext& server_context) {
  if (!remote_jwks.has_retry_policy()) {
    return Router::RetryPolicyConstSharedPtr{};
  }
  if (absl::Status status = Http::Utility::validateCoreRetryPolicy(remote_jwks.retry_policy());
      !status.ok()) {
    return status;
  }
  const envoy::config::route::v3::RetryPolicy route_retry_policy =
      Http::Utility::convertCoreToRouteRetryPolicy(remote_jwks.retry_policy(),
                                                   "5xx,gateway-error,connect-failure,reset");
  // RetryPolicyImpl::create only fails when max_interval < base_interval, which the
  // validateCoreRetryPolicy check above already rejects, so it cannot fail here.
  auto policy_or_error = Router::RetryPolicyImpl::create(
      route_retry_policy, ProtobufMessage::getNullValidationVisitor(), server_context);
  return Router::RetryPolicyConstSharedPtr{std::move(policy_or_error.value())};
}

// Raises a cache_duration or failed_refetch_duration below one second up to one second. The
// background fetcher waits these durations between fetches, so a shorter value would fetch again
// with no real pause. Returns an error only for an out-of-range duration.
// TODO(kanurag94): remove once the background fetcher floors these durations itself.
absl::Status clampRefetchDurationsToOneSecond(RemoteJwks& remote_jwks) {
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

} // namespace

// Fetches a remote JWKS in the background, at startup and every cache_duration, so verification
// during the handshake stays synchronous. Holds its own copy of the keys for worker threads to
// read safely.
class JwtHandshakeValidator::RemoteJwksProvider {
public:
  RemoteJwksProvider(const RemoteJwks& remote_jwks, Router::RetryPolicyConstSharedPtr retry_policy,
                     Server::Configuration::FactoryContext& context,
                     JwksFetcherFactory create_fetcher_fn)
      : remote_jwks_(remote_jwks), stats_{ALL_REVERSE_TUNNEL_JWKS_STATS(POOL_COUNTER_PREFIX(
                                       context.scope(), "reverse_tunnel.handshake."))} {
    JwksFetcherFactory fetcher_cb =
        create_fetcher_fn
            ? std::move(create_fetcher_fn)
            : JwksFetcherFactory(&Extensions::HttpFilters::Common::JwksFetcher::create);
    async_fetcher_ = std::make_unique<HttpFilters::JwtAuthn::JwksAsyncFetcher>(
        remote_jwks_, std::move(retry_policy), context, std::move(fetcher_cb),
        stats_.jwt_jwks_fetch_success_, stats_.jwt_jwks_fetch_failed_,
        [this](JwtVerify::JwksPtr&& jwks) {
          Thread::LockGuard lock(mutex_);
          jwks_ = std::move(jwks);
        });
  }

  JwksConstSharedPtr currentJwks() {
    Thread::LockGuard lock(mutex_);
    return jwks_;
  }

private:
  // Kept here because JwksAsyncFetcher holds it by const reference and must outlive the fetcher.
  const RemoteJwks remote_jwks_;
  ReverseTunnelJwksStats stats_;
  // Guards the fetched keys. Handshakes are per-connection, so this lock is rarely contended.
  Thread::MutexBasicLockable mutex_;
  JwksConstSharedPtr jwks_ ABSL_GUARDED_BY(mutex_);
  // Declared last so it is destroyed first. Its callback captures this, so the fetcher must stop
  // before the members it touches are gone.
  HttpFilters::JwtAuthn::JwksAsyncFetcherPtr async_fetcher_;
};

absl::StatusOr<std::unique_ptr<JwtHandshakeValidator>>
JwtHandshakeValidator::create(const JwtHandshakeValidationProto& jwt,
                              Server::Configuration::FactoryContext& context,
                              JwksFetcherFactory create_fetcher_fn) {
  // An issuer is required. Without one, a validly signed token from any issuer would be accepted.
  if (jwt.issuer().empty()) {
    return absl::InvalidArgumentError("reverse_tunnel jwt_validation: `issuer` is required");
  }

  JwksConstSharedPtr local_jwks;
  std::unique_ptr<RemoteJwksProvider> remote_provider;
  switch (jwt.jwks_source_specifier_case()) {
  case JwtHandshakeValidationProto::kLocalJwks: {
    auto jwks_or_error = Config::DataSource::read(jwt.local_jwks(), /*allow_empty=*/false,
                                                  context.serverFactoryContext().api());
    if (!jwks_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validation: failed to load local_jwks: {}",
                      jwks_or_error.status().message()));
    }
    auto jwks = JwtVerify::Jwks::createFrom(jwks_or_error.value(), JwtVerify::Jwks::JWKS);
    if (jwks->getStatus() != JwtVerify::Status::Ok) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validation: invalid local_jwks: {}",
                      JwtVerify::getStatusString(jwks->getStatus())));
    }
    local_jwks = std::move(jwks);
    break;
  }
  case JwtHandshakeValidationProto::kRemoteJwks: {
    const RemoteJwks remote_jwks = toJwtAuthnRemoteJwks(jwt.remote_jwks());
    Http::Utility::Url url;
    if (!url.initialize(remote_jwks.http_uri().uri(), /*is_connect_request=*/false)) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validation: invalid remote_jwks http_uri: '{}'",
                      remote_jwks.http_uri().uri()));
    }
    auto retry_policy_or_error =
        buildRemoteJwksRetryPolicy(remote_jwks, context.serverFactoryContext());
    if (!retry_policy_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validation: invalid remote_jwks retry_policy: {}",
                      retry_policy_or_error.status().message()));
    }
    // The handshake never fetches on demand, so force background fetch on. Without async_fetch the
    // fetcher would do nothing and no keys would ever load.
    RemoteJwks normalized = remote_jwks;
    if (!normalized.has_async_fetch()) {
      normalized.mutable_async_fetch();
    }
    if (absl::Status status = clampRefetchDurationsToOneSecond(normalized); !status.ok()) {
      return absl::InvalidArgumentError(fmt::format(
          "reverse_tunnel jwt_validation: invalid remote_jwks duration: {}", status.message()));
    }
    remote_provider =
        std::make_unique<RemoteJwksProvider>(normalized, std::move(retry_policy_or_error.value()),
                                             context, std::move(create_fetcher_fn));
    break;
  }
  case JwtHandshakeValidationProto::JWKS_SOURCE_SPECIFIER_NOT_SET:
    return absl::InvalidArgumentError(
        "reverse_tunnel jwt_validation: a JWKS source (local_jwks or remote_jwks) is required");
  }

  return std::unique_ptr<JwtHandshakeValidator>(
      new JwtHandshakeValidator(jwt, std::move(local_jwks), std::move(remote_provider)));
}

JwtHandshakeValidator::JwtHandshakeValidator(const JwtHandshakeValidationProto& jwt,
                                             JwksConstSharedPtr local_jwks,
                                             std::unique_ptr<RemoteJwksProvider> remote_provider)
    : issuer_(jwt.issuer()), audiences_([&jwt]() {
        std::vector<std::string> audiences(jwt.audiences().begin(), jwt.audiences().end());
        return JwtVerify::CheckAudience(audiences);
      }()),
      clock_skew_seconds_(jwt.clock_skew_seconds() > 0 ? jwt.clock_skew_seconds()
                                                       : JwtVerify::kClockSkewInSecond),
      token_header_(!jwt.token_header().empty() ? Http::LowerCaseString(jwt.token_header())
                                                : Http::LowerCaseString("authorization")),
      claims_namespace_(!jwt.claims_metadata_namespace().empty()
                            ? jwt.claims_metadata_namespace()
                            : "envoy.filters.network.reverse_tunnel.jwt"),
      required_(!jwt.allow_missing_or_failed()), local_jwks_(std::move(local_jwks)),
      remote_provider_(std::move(remote_provider)) {}

JwtHandshakeValidator::~JwtHandshakeValidator() = default;

JwksConstSharedPtr JwtHandshakeValidator::currentJwks() const {
  if (remote_provider_ != nullptr) {
    return remote_provider_->currentJwks();
  }
  return local_jwks_;
}

bool JwtHandshakeValidator::verify(const Http::RequestHeaderMap& headers,
                                   StreamInfo::StreamInfo& stream_info) const {
  // Extract the token from the configured header.
  const auto token_values = headers.get(token_header_);
  if (token_values.empty()) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: missing token header '{}'", token_header_.get());
    return false;
  }
  // If several token headers are present, use the first, as jwt_authn does.
  if (token_values.size() > 1) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: {} values on token header '{}'; using the first",
              token_values.size(), token_header_.get());
  }
  absl::string_view token = token_values[0]->value().getStringView();
  // Strip a leading "Bearer " prefix when present (case-insensitive), matching jwt_authn.
  static constexpr absl::string_view bearer_prefix = "Bearer ";
  if (absl::StartsWithIgnoreCase(token, bearer_prefix)) {
    token = token.substr(bearer_prefix.size());
  }

  JwtVerify::Jwt jwt;
  if (jwt.parseFromString(std::string(token)) != JwtVerify::Status::Ok) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: token parse failed");
    return false;
  }

  // Verify the signature and the time constraints, meaning exp and nbf, against the configured JWKS
  // before trusting any claim in the payload. With remote_jwks the keys may not be fetched yet, or
  // a refresh may be failing with nothing cached. Treat either case as a verification failure.
  const JwksConstSharedPtr jwks = currentJwks();
  if (jwks == nullptr) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: no JWKS available yet");
    return false;
  }
  const uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                           stream_info.timeSource().systemTime().time_since_epoch())
                           .count();
  const JwtVerify::Status status = JwtVerify::verifyJwt(jwt, *jwks, now, clock_skew_seconds_);
  if (status != JwtVerify::Status::Ok) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: verification failed: {}",
              JwtVerify::getStatusString(status));
    return false;
  }

  // An expiration is required. A token without exp would otherwise be valid forever.
  if (jwt.exp_ == 0) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: token has no `exp` claim");
    return false;
  }

  // The issuer must match. Checked after signature verification so the payload can be trusted.
  if (jwt.iss_ != issuer_) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: issuer mismatch (expected '{}', got '{}')", issuer_,
              jwt.iss_);
    return false;
  }

  // Check the audience, normalized (scheme / trailing slash) like jwt_authn. Passes when no
  // audience is configured.
  if (!audiences_.areAudiencesAllowed(jwt.audiences_)) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: audience not allowed");
    return false;
  }

  // The token is verified. Publish the claims as dynamic metadata so the validation block can match
  // a claimed identifier against a real claim with %DYNAMIC_METADATA(namespace:claim)%. This runs
  // just before validateIdentifiers(), which is where those bindings are read.
  stream_info.setDynamicMetadata(claims_namespace_, jwt.payload_pb_);
  ENVOY_LOG(debug, "reverse_tunnel: jwt: verified; claims published to namespace '{}'",
            claims_namespace_);
  return true;
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
