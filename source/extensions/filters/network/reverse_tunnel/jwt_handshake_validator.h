#pragma once

#include <functional>
#include <memory>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/jwt/check_audience.h"
#include "source/common/jwt/jwks.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// JWKS shared read-only across worker threads for handshake token verification.
using JwksConstSharedPtr = std::shared_ptr<const JwtVerify::Jwks>;

// Creates a JwksFetcher for a remote_jwks source. Overridable in tests; production uses the real
// Extensions::HttpFilters::Common::JwksFetcher. Structurally the same as jwt_authn's
// CreateJwksFetcherCb, but declared here so this header does not include jwks_async_fetcher.h.
using JwksFetcherFactory = std::function<Extensions::HttpFilters::Common::JwksFetcherPtr(
    Upstream::ClusterManager&, Router::RetryPolicyConstSharedPtr,
    const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks&)>;

// Verifies the reverse tunnel handshake bearer token. Owns the JWKS source: inline keys for
// local_jwks, or a background fetcher for remote_jwks. Verification stays synchronous in both
// cases, so the L4 filter decides accept or reject before registering the socket.
class JwtHandshakeValidator : public Logger::Loggable<Logger::Id::filter> {
public:
  // Validates the config and builds the validator. A local_jwks is parsed now; a remote_jwks starts
  // a background fetcher. Returns an error for a missing issuer, an invalid JWKS, a bad remote_jwks
  // URI, or a bad retry policy.
  static absl::StatusOr<std::unique_ptr<JwtHandshakeValidator>>
  create(const envoy::extensions::filters::network::reverse_tunnel::v3::JwtHandshakeValidation& jwt,
         Server::Configuration::FactoryContext& context,
         JwksFetcherFactory create_fetcher_fn = nullptr);

  ~JwtHandshakeValidator();

  // Whether a missing or invalid token must reject the handshake (i.e. not audit mode).
  bool required() const { return required_; }

  // Verifies the token against the configured JWKS, issuer, audiences and time constraints (a token
  // without an exp claim is rejected). On success, publishes the verified claims as dynamic
  // metadata under the configured namespace so the validation block can bind claimed ids to
  // verified claims via %DYNAMIC_METADATA(namespace:claim)%. Returns true iff the token verified
  // and its claims were published.
  bool verify(const Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& stream_info) const;

private:
  // Background fetcher for remote_jwks. Defined in the .cc; referenced here only by pointer.
  class RemoteJwksProvider;

  JwtHandshakeValidator(
      const envoy::extensions::filters::network::reverse_tunnel::v3::JwtHandshakeValidation& jwt,
      JwksConstSharedPtr local_jwks, std::unique_ptr<RemoteJwksProvider> remote_provider);

  // The keys currently in effect: the inline keys for local_jwks, or the most recently fetched keys
  // for remote_jwks (nullptr until the first successful fetch).
  JwksConstSharedPtr currentJwks() const;

  const std::string issuer_;
  const JwtVerify::CheckAudience audiences_;
  const uint64_t clock_skew_seconds_;
  const Http::LowerCaseString token_header_;
  const std::string claims_namespace_;
  const bool required_;
  // Inline JWKS (local_jwks); nullptr for remote_jwks.
  const JwksConstSharedPtr local_jwks_;
  // Background remote-JWKS fetcher (remote_jwks); nullptr for local_jwks.
  const std::unique_ptr<RemoteJwksProvider> remote_provider_;
};

using JwtHandshakeValidatorPtr = std::unique_ptr<JwtHandshakeValidator>;

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
