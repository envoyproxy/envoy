#include "extensions/filters/http/jwt_authn/verify_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

Http::HeaderMap& VerifyContext::headers() const { return headers_; }

VerifierCallbacks* VerifyContext::callback() const { return callback_; }

CompletionState& VerifyContext::getCompletionState(const Verifier* verifier) {
  return completion_states_[verifier];
}

void VerifyContext::storeAuth(AuthenticatorPtr&& auth) { auths_.emplace_back(std::move(auth)); }

void VerifyContext::cancel() {
  for (const auto& it : auths_) {
    it->onDestroy();
  }
  auths_.clear();
}

VerifyContextSharedPtr VerifyContext::create(Http::HeaderMap& headers,
                                             VerifierCallbacks* callback) {
  return std::make_shared<VerifyContext>(headers, callback);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
