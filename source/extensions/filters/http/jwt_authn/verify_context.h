#pragma once

#include "extensions/filters/http/jwt_authn/authenticator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Struct to keep track of verifier completed and responded state for a request.
 */
struct CompletionState {
  // if verifier node has responded to a request or not.
  bool is_completed_{false};
  // number of completed inner verifier for an any/all verifier.
  std::size_t number_completed_children_{0};
};

class VerifyContext;
typedef std::shared_ptr<VerifyContext> VerifyContextSharedPtr;

class Verifier;
class VerifierCallbacks;

/**
 * This object holds dynamic data generated on each request for verifiers.
 */
class VerifyContext {
public:
  VerifyContext(Http::HeaderMap& headers, VerifierCallbacks* callback)
      : headers_(headers), callback_(callback) {}

  virtual ~VerifyContext() {}

  /**
   * Returns the request headers wrapped in this context.
   *
   * @return the request headers.
   */
  Http::HeaderMap& headers() const;

  /**
   * Returns the original request callback wrapped in this context.
   *
   * @returns the original request callback.
   */
  VerifierCallbacks* callback() const;

  /**
   * Get Response data which can be used to check if a verifier node has responded or not.
   *
   * @param verifier verifier node pointer.
   */
  CompletionState& getCompletionState(const Verifier* verifier);

  /**
   * Stores an authenticator object for this request.
   *
   * @param auth the authenticator object pointer.
   */
  void storeAuth(AuthenticatorPtr&& auth);

  /**
   * Cancel any pending reuqets for this context.
   */
  void cancel();

  /**
   * Factory method for creating a new context object.
   */
  static VerifyContextSharedPtr create(Http::HeaderMap& headers, VerifierCallbacks* callback);

private:
  Http::HeaderMap& headers_;
  VerifierCallbacks* callback_;
  std::unordered_map<const Verifier*, CompletionState> completion_states_;
  std::vector<AuthenticatorPtr> auths_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
