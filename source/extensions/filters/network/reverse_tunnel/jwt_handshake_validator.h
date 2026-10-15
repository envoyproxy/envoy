#pragma once

#include <memory>

#include "envoy/extensions/filters/common/jwks/v3/jwt_handshake.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/jwt/check_audience.h"
#include "source/extensions/filters/common/jwks/jwks_provider.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// Re-exported so the filter and its tests use one name for the fetcher factory.
using JwksFetcherFactory = Extensions::Filters::Common::Jwks::JwksFetcherFactory;

// Verifies the reverse tunnel handshake bearer token. It holds the JWKS through a JwksProvider,
// inline keys for local_jwks or a background fetcher for remote_jwks. Verification stays
// synchronous either way, so the L4 filter accepts or rejects before registering the socket.
class JwtHandshakeValidator : public Logger::Loggable<Logger::Id::filter> {
public:
  // Validates the config and builds the validator. A local_jwks is parsed now. A remote_jwks starts
  // a background fetcher. Returns an error for a missing issuer, an invalid JWKS, a bad remote_jwks
  // URI, or a bad retry policy.
  static absl::StatusOr<std::unique_ptr<JwtHandshakeValidator>>
  create(const envoy::extensions::filters::common::jwks::v3::JwtHandshakeValidator& jwt,
         Server::Configuration::FactoryContext& context,
         JwksFetcherFactory create_fetcher_fn = nullptr);

  ~JwtHandshakeValidator();

  // Whether a missing or invalid token must reject the handshake (i.e. not audit mode).
  bool required() const { return required_; }

  // Verifies the token against the configured JWKS, issuer, audiences, and exp/nbf. A token without
  // an exp claim is rejected. On success it publishes the verified claims as dynamic metadata under
  // the configured namespace, so the validation block can bind claimed ids to real claims. Returns
  // true only if the token verified.
  bool verify(const Http::RequestHeaderMap& headers, StreamInfo::StreamInfo& stream_info) const;

private:
  JwtHandshakeValidator(
      const envoy::extensions::filters::common::jwks::v3::JwtHandshakeValidator& jwt,
      Extensions::Filters::Common::Jwks::JwksProviderSharedPtr provider);

  const std::string issuer_;
  const JwtVerify::CheckAudience audiences_;
  const uint64_t clock_skew_seconds_;
  const Http::LowerCaseString token_header_;
  const std::string claims_namespace_;
  const bool required_;
  // The JWKS source, inline for local_jwks or a background fetcher for remote_jwks. jwks() is
  // nullptr until the first successful remote fetch.
  const Extensions::Filters::Common::Jwks::JwksProviderSharedPtr provider_;
};

using JwtHandshakeValidatorPtr = std::unique_ptr<JwtHandshakeValidator>;

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
