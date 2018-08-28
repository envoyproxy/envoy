#pragma once

#include <functional>

#include "envoy/http/header_map.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/extractor.h"
#include "extensions/filters/http/jwt_authn/verifier_callbacks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class VerifyContext;
typedef std::unique_ptr<VerifyContext> VerifyContextPtr;

/**
 * This object holds dynamic data generated on each request for verifiers.
 */
class VerifyContext {
public:
  virtual ~VerifyContext() {}

  /**
   * Returns the request headers wrapped in this context.
   *
   * @return the request headers.
   */
  virtual Http::HeaderMap& headers() const PURE;

  /**
   * Returns the original request callback wrapped in this context.
   *
   * @returns the original request callback.
   */
  virtual VerifierCallbacks* callback() const PURE;

  /**
   * Mark a verifier node as responded.
   *
   * @param elem pointer to a verifier node.
   */
  virtual void responded(void* elem) PURE;

  /**
   * Check to see if a verifier node has responded.
   *
   * @param elem pointer to a verifier node.
   */
  virtual bool hasResponded(void* elem) const PURE;

  /**
   * Set count for number of inner verifier node processed to zero.
   *
   * @param elem pointer to a verifier node.
   */
  virtual void resetCount(void* elem) PURE;

  /**
   * Return the number of inner verifier node's count for this context.
   *
   * @param elem pointer to a verifier node.
   */
  virtual std::size_t incrementAndGetCount(void* elem) PURE;

  /**
   * Stores an authenticator object for this request.
   *
   * @param auth the authenticator object pointer.
   */
  virtual void addAuth(AuthenticatorPtr&& auth) PURE;

  /**
   * Cancel any pending reuqets for this context.
   */
  virtual void cancel() PURE;

  /**
   * Factory method for creating a new context object.
   */
  static VerifyContextPtr create(Http::HeaderMap& headers, VerifierCallbacks* callback);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
