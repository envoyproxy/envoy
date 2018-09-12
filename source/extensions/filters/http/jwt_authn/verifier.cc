#include "extensions/filters/http/jwt_authn/verifier.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementAndList;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementOrList;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Struct to keep track of verifier completed and responded state for a request.
 */
struct CompletionState {
  // if verifier node has responded to a request or not.
  bool is_completed_{false};
  // number of completed inner verifier for an any/all verifier.
  std::size_t number_completed_children_{0};
};

class ContextImpl : public Verifier::Context {
public:
  ContextImpl(Http::HeaderMap& headers, Verifier::Callbacks* callback)
      : headers_(headers), callback_(callback) {}

  Http::HeaderMap& headers() const override { return headers_; }

  Verifier::Callbacks* callback() const override { return callback_; }

  void cancel() override {
    for (const auto& it : auths_) {
      it->onDestroy();
    }
    auths_.clear();
  }

  // Get Response data which can be used to check if a verifier node has responded or not.
  CompletionState& getCompletionState(const Verifier* verifier) {
    return completion_states_[verifier];
  }

  // Stores an authenticator object for this request.
  void storeAuth(AuthenticatorPtr&& auth) { auths_.emplace_back(std::move(auth)); }

private:
  Http::HeaderMap& headers_;
  Verifier::Callbacks* callback_;
  std::unordered_map<const Verifier*, CompletionState> completion_states_;
  std::vector<AuthenticatorPtr> auths_;
};

// base verifier for provider_name, provider_and_audiences, and allow_missing_or_failed.
class BaseVerifierImpl : public Verifier {
public:
  BaseVerifierImpl(const BaseVerifierImpl* parent) : parent_(parent) {}

  void completeWithStatus(Status status, ContextImpl& context) const {
    if (parent_ != nullptr) {
      return parent_->onComplete(status, context);
    }
    return context.callback()->onComplete(status);
  }

  // Check if next verifier should be notified of status, or if no next verifier exists signal
  // callback in context.
  virtual void onComplete(const Status& status, ContextImpl& context) const {
    auto& completion_state = context.getCompletionState(this);
    if (!completion_state.is_completed_) {
      completion_state.is_completed_ = true;
      completeWithStatus(status, context);
    }
  }

protected:
  // The parent group verifier.
  const BaseVerifierImpl* const parent_;
};

// Provider specific verifier
class ProviderVerifierImpl : public BaseVerifierImpl {
public:
  ProviderVerifierImpl(const CheckAudienceConstSharedPtr audiences, const AuthFactory& factory,
                       const JwtProvider& provider, const BaseVerifierImpl* parent)
      : BaseVerifierImpl(parent), audiences_(audiences), auth_factory_(factory),
        extractor_(Extractor::create(provider)), issuer_(provider.issuer()) {}

  void verify(ContextSharedPtr context) const override {
    auto& ctximpl = static_cast<ContextImpl&>(*context);
    auto auth = auth_factory_.create(audiences_, absl::optional<std::string>{issuer_}, false);
    extractor_->sanitizePayloadHeaders(ctximpl.headers());
    auth->verify(
        ctximpl.headers(), extractor_->extract(ctximpl.headers()),
        [=](const Status& status) { onComplete(status, static_cast<ContextImpl&>(*context)); });
    if (!ctximpl.getCompletionState(this).is_completed_) {
      ctximpl.storeAuth(std::move(auth));
    }
  }

private:
  const CheckAudienceConstSharedPtr audiences_;
  const AuthFactory& auth_factory_;
  const ExtractorConstPtr extractor_;
  const std::string issuer_;
};

// Allow missing or failed verifier
class AllowFailedVerifierImpl : public BaseVerifierImpl {
public:
  AllowFailedVerifierImpl(const AuthFactory& factory, const Extractor& extractor,
                          const BaseVerifierImpl* parent)
      : BaseVerifierImpl(parent), auth_factory_(factory), extractor_(extractor) {}

  void verify(ContextSharedPtr context) const override {
    auto& ctximpl = static_cast<ContextImpl&>(*context);
    auto auth = auth_factory_.create({}, absl::nullopt, true);
    extractor_.sanitizePayloadHeaders(ctximpl.headers());
    auth->verify(
        ctximpl.headers(), extractor_.extract(ctximpl.headers()),
        [=](const Status& status) { onComplete(status, static_cast<ContextImpl&>(*context)); });
    if (!ctximpl.getCompletionState(this).is_completed_) {
      ctximpl.storeAuth(std::move(auth));
    }
  }

private:
  const AuthFactory& auth_factory_;
  const Extractor& extractor_;
};

VerifierPtr innerCreate(const JwtRequirement& requirement,
                        const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                        const AuthFactory& factory, const Extractor& extractor,
                        const BaseVerifierImpl* parent);

// Base verifier for requires all or any.
class BaseGroupVerifierImpl : public BaseVerifierImpl {
public:
  BaseGroupVerifierImpl(const BaseVerifierImpl* parent) : BaseVerifierImpl(parent) {}

  void verify(ContextSharedPtr context) const override {
    auto& ctximpl = static_cast<ContextImpl&>(*context);
    for (const auto& it : verifiers_) {
      if (ctximpl.getCompletionState(this).is_completed_) {
        return;
      }
      it->verify(context);
    }
  }

protected:
  // The list of requirement verifiers
  std::vector<VerifierPtr> verifiers_;
};

// Requires any verifier.
class AnyVerifierImpl : public BaseGroupVerifierImpl {
public:
  AnyVerifierImpl(const JwtRequirementOrList& or_list, const AuthFactory& factory,
                  const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                  const Extractor& extractor_for_allow_fail, const BaseVerifierImpl* parent)
      : BaseGroupVerifierImpl(parent) {
    for (const auto& it : or_list.requirements()) {
      verifiers_.emplace_back(innerCreate(it, providers, factory, extractor_for_allow_fail, this));
    }
  }

  void onComplete(const Status& status, ContextImpl& context) const override {
    auto& completion_state = context.getCompletionState(this);
    if (completion_state.is_completed_) {
      return;
    }
    if (++completion_state.number_completed_children_ == verifiers_.size() ||
        Status::Ok == status) {
      completion_state.is_completed_ = true;
      completeWithStatus(status, context);
    }
  }
};

// Requires all verifier
class AllVerifierImpl : public BaseGroupVerifierImpl {
public:
  AllVerifierImpl(const JwtRequirementAndList& and_list, const AuthFactory& factory,
                  const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                  const Extractor& extractor_for_allow_fail, const BaseVerifierImpl* parent)
      : BaseGroupVerifierImpl(parent) {
    for (const auto& it : and_list.requirements()) {
      verifiers_.emplace_back(innerCreate(it, providers, factory, extractor_for_allow_fail, this));
    }
  }

  void onComplete(const Status& status, ContextImpl& context) const override {
    auto& completion_state = context.getCompletionState(this);
    if (completion_state.is_completed_) {
      return;
    }
    if (++completion_state.number_completed_children_ == verifiers_.size() ||
        Status::Ok != status) {
      completion_state.is_completed_ = true;
      completeWithStatus(status, context);
    }
  }
};

// Match all, for requirement not set
class AllowAllVerifierImpl : public BaseVerifierImpl {
public:
  AllowAllVerifierImpl(const BaseVerifierImpl* parent) : BaseVerifierImpl(parent) {}

  void verify(ContextSharedPtr context) const override {
    onComplete(Status::Ok, static_cast<ContextImpl&>(*context));
  }

  void onComplete(const Status& status, ContextImpl& context) const override {
    completeWithStatus(status, context);
  }
};

VerifierPtr innerCreate(const JwtRequirement& requirement,
                        const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                        const AuthFactory& factory, const Extractor& extractor_for_allow_fail,
                        const BaseVerifierImpl* parent) {
  std::string provider_name;
  std::vector<std::string> audiences;
  switch (requirement.requires_type_case()) {
  case JwtRequirement::RequiresTypeCase::kProviderName:
    provider_name = requirement.provider_name();
    break;
  case JwtRequirement::RequiresTypeCase::kProviderAndAudiences:
    for (const auto& it : requirement.provider_and_audiences().audiences()) {
      audiences.emplace_back(it);
    }
    provider_name = requirement.provider_and_audiences().provider_name();
    break;
  case JwtRequirement::RequiresTypeCase::kRequiresAny:
    return std::make_unique<AnyVerifierImpl>(requirement.requires_any(), factory, providers,
                                             extractor_for_allow_fail, parent);
  case JwtRequirement::RequiresTypeCase::kRequiresAll:
    return std::make_unique<AllVerifierImpl>(requirement.requires_all(), factory, providers,
                                             extractor_for_allow_fail, parent);
  case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
    return std::make_unique<AllowFailedVerifierImpl>(factory, extractor_for_allow_fail, parent);
  case JwtRequirement::RequiresTypeCase::REQUIRES_TYPE_NOT_SET:
    return std::make_unique<AllowAllVerifierImpl>(parent);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const auto& it = providers.find(provider_name);
  if (it == providers.end()) {
    throw EnvoyException(fmt::format("Required provider ['{}'] is not configured.", provider_name));
  }
  if (audiences.empty()) {
    return std::make_unique<ProviderVerifierImpl>(nullptr, factory, it->second, parent);
  }
  return std::make_unique<ProviderVerifierImpl>(
      std::make_shared<const ::google::jwt_verify::CheckAudience>(audiences), factory, it->second,
      parent);
}

} // namespace

ContextSharedPtr Verifier::createContext(Http::HeaderMap& headers, Callbacks* callback) {
  return std::make_shared<ContextImpl>(headers, callback);
}

VerifierPtr Verifier::create(const JwtRequirement& requirement,
                             const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                             const AuthFactory& factory,
                             const Extractor& extractor_for_allow_fail) {
  return innerCreate(requirement, providers, factory, extractor_for_allow_fail, nullptr);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
