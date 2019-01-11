#pragma once

#include "extensions/filters/http/jwt_authn/authenticator.h"

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

  /**
   * Handle for notifying Verifier callers of request completion.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() {}

    /**
     * Successfully verified JWT payload are stored in the struct with its
     * *fields* containing **issuer** as keys and **payload** as string values
     * This function is called before onComplete() function.
     * It will not be called if no payload to write.
     */
    virtual void setPayload(const ProtobufWkt::Struct& payload) PURE;

    /**
     * Called on completion of request.
     *
     * @param status the status of the request.
     */
    virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
  };

  // Context object to hold data needed for verifier.
  class Context {
  public:
    virtual ~Context() {}

    /**
     * Returns the request headers wrapped in this context.
     *
     * @return the request headers.
     */
    virtual Http::HeaderMap& headers() const PURE;

    /**
     * Returns the request callback wrapped in this context.
     *
     * @returns the request callback.
     */
    virtual Callbacks* callback() const PURE;

    /**
     * Cancel any pending reuqets for this context.
     */
    virtual void cancel() PURE;
  };

  typedef std::shared_ptr<Context> ContextSharedPtr;

  // Verify all tokens on headers, and signal the caller with callback.
  virtual void verify(ContextSharedPtr context) const PURE;

  // Factory method for creating verifiers.
  static VerifierPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement& requirement,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         const AuthFactory& factory, const Extractor& extractor_for_allow_fail);

  // Factory method for creating verifier contexts.
  static ContextSharedPtr createContext(Http::HeaderMap& headers, Callbacks* callback);
};

typedef std::shared_ptr<Verifier::Context> ContextSharedPtr;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
