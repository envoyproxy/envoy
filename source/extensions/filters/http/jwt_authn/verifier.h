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
  virtual void verify(VerifyContext& context) PURE;
  // Set the verifier's parent group verifier.
  virtual void registerParent(Verifier* parent) PURE;
  // Check if next verifier should be notified of status, or if no next verifier exists signal
  // callback in context.
  virtual void onComplete(const ::google::jwt_verify::Status& status, VerifyContext& context) PURE;
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
