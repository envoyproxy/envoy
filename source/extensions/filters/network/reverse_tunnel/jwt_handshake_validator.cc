#include "source/extensions/filters/network/reverse_tunnel/jwt_handshake_validator.h"

#include <vector>

#include "envoy/stats/stats_macros.h"

#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"
#include "source/common/jwt/verify.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

namespace CommonJwks = ::Envoy::Extensions::Filters::Common::Jwks;

namespace {

using JwtHandshakeValidatorProto =
    envoy::extensions::filters::common::jwks::v3::JwtHandshakeValidator;

// Counters for the remote JWKS background fetcher. They live on the config and are updated on the
// main thread, unlike the per-connection handshake counters in ReverseTunnelStats.
#define ALL_REVERSE_TUNNEL_JWKS_STATS(COUNTER)                                                     \
  COUNTER(jwt_jwks_fetch_success)                                                                  \
  COUNTER(jwt_jwks_fetch_failed)

struct ReverseTunnelJwksStats {
  ALL_REVERSE_TUNNEL_JWKS_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace

absl::StatusOr<std::unique_ptr<JwtHandshakeValidator>>
JwtHandshakeValidator::create(const JwtHandshakeValidatorProto& jwt,
                              Server::Configuration::FactoryContext& context,
                              JwksFetcherFactory create_fetcher_fn) {
  // An issuer is required. Without one, a validly signed token from any issuer would be accepted.
  if (jwt.issuer().empty()) {
    return absl::InvalidArgumentError("reverse_tunnel jwt_validator: `issuer` is required");
  }

  CommonJwks::JwksProviderSharedPtr provider;
  switch (jwt.jwks_source_specifier_case()) {
  case JwtHandshakeValidatorProto::kLocalJwks: {
    auto provider_or_error = CommonJwks::createStaticJwksProvider(
        jwt.local_jwks(), context.serverFactoryContext().api());
    if (!provider_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validator: {}", provider_or_error.status().message()));
    }
    provider = std::move(provider_or_error.value());
    break;
  }
  case JwtHandshakeValidatorProto::kRemoteJwks: {
    // The fetch counters are owned by the config scope, so they outlive the provider that keeps
    // references to them.
    ReverseTunnelJwksStats stats{ALL_REVERSE_TUNNEL_JWKS_STATS(
        POOL_COUNTER_PREFIX(context.scope(), "reverse_tunnel.handshake."))};
    auto provider_or_error = CommonJwks::createRemoteJwksProvider(
        jwt.remote_jwks(), context, std::move(create_fetcher_fn), stats.jwt_jwks_fetch_success_,
        stats.jwt_jwks_fetch_failed_);
    if (!provider_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("reverse_tunnel jwt_validator: {}", provider_or_error.status().message()));
    }
    provider = std::move(provider_or_error.value());
    break;
  }
  case JwtHandshakeValidatorProto::JWKS_SOURCE_SPECIFIER_NOT_SET:
    return absl::InvalidArgumentError(
        "reverse_tunnel jwt_validator: a JWKS source (local_jwks or remote_jwks) is required");
  }

  return std::unique_ptr<JwtHandshakeValidator>(
      new JwtHandshakeValidator(jwt, std::move(provider)));
}

JwtHandshakeValidator::JwtHandshakeValidator(const JwtHandshakeValidatorProto& jwt,
                                             CommonJwks::JwksProviderSharedPtr provider)
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
      required_(!jwt.allow_missing_or_failed()), provider_(std::move(provider)) {}

JwtHandshakeValidator::~JwtHandshakeValidator() = default;

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
  const CommonJwks::JwksConstSharedPtr jwks = provider_->jwks();
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
  // a claimed identifier against a real claim with %DYNAMIC_METADATA(namespace:claim)%.
  stream_info.setDynamicMetadata(claims_namespace_, jwt.payload_pb_);
  ENVOY_LOG(debug, "reverse_tunnel: jwt: verified; claims published to namespace '{}'",
            claims_namespace_);
  return true;
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
