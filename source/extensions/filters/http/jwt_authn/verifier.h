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
 * Supports verification of JWTs with configured requirments.
 */
class Verifier {
public:
  virtual ~Verifier() {}

  // Verify all tokens on headers, and signal the caller with callback.
  virtual void verify(VerifyContextSharedPtr context) const PURE;

  // Factory method for creating verifiers.
  static VerifierPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement& requirement,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         const AuthFactory& factory, const Extractor& extractor);
};

/**
 * Handle for notifying Verifier callers of request completion.
 */
class VerifierCallbacks {
public:
  virtual ~VerifierCallbacks() {}

  /**
   * Called on completion of request.
   *
   * @param status the status of the request.
   */
  virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
