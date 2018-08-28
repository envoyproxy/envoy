#include "extensions/filters/http/jwt_authn/verify_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class VerifyContextImpl : public VerifyContext {
public:
  VerifyContextImpl(Http::HeaderMap& headers, VerifierCallbacks* callback)
      : headers_(headers), callback_(callback) {}

  Http::HeaderMap& headers() const override { return headers_; }

  VerifierCallbacks* callback() const override { return callback_; }

  void responded(void* elem) override { responded_set_.insert(elem); }

  bool hasResponded(void* elem) const override {
    return responded_set_.find(elem) != responded_set_.end();
  }

  void resetCount(void* elem) override { counts_[elem] = 0; }

  std::size_t incrementAndGetCount(void* elem) override { return ++counts_[elem]; }

  void addAuth(AuthenticatorPtr&& auth) override { auths_.push_back(std::move(auth)); }

  void cancel() override {
    for (const auto& it : auths_) {
      it->onDestroy();
    }
    auths_.clear();
  }

private:
  Http::HeaderMap& headers_;
  VerifierCallbacks* callback_;
  std::unordered_set<void*> responded_set_;
  std::unordered_map<void*, std::size_t> counts_;
  std::vector<AuthenticatorPtr> auths_;
};

} // namespace

VerifyContextPtr VerifyContext::create(Http::HeaderMap& headers, VerifierCallbacks* callback) {
  return std::make_unique<VerifyContextImpl>(headers, callback);
}
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
