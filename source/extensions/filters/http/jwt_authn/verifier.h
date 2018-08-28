#pragma once

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/verify_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Verifier;
typedef std::unique_ptr<Verifier> VerifierPtr;
/**
 * Interface for authenticator factory.
 */
class AuthFactory {
public:
  virtual ~AuthFactory() {}

  // Factory method for creating authenticator, and populate it with provider config.
  virtual AuthenticatorPtr create(const std::vector<std::string>& audiences,
                                  const absl::optional<std::string>& issuer,
                                  bool allow_failed) const PURE;
};

/**
 * Supports verification of JWTs with configured requirments.
 */
class Verifier {
public:
  virtual ~Verifier() {}

  // Verify all tokens on headers, and signal the caller with callback.
  virtual void verify(VerifyContext& context) PURE;
  // Set the next verifier callback.
  virtual void registerCallback(VerifierCallbacks* callback) PURE;

  // Factory method for creating verifiers.
  static VerifierPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement& requirement,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         const AuthFactory& factory, const Extractor& extractor);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
