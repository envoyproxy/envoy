#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "jwt_verify_lib/check_audience.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtProvider;
using envoy::extensions::filters::http::jwt_authn::v3::JwtRequirement;
using envoy::extensions::filters::http::jwt_authn::v3::JwtRequirementAndList;
using envoy::extensions::filters::http::jwt_authn::v3::JwtRequirementOrList;
using ::google::jwt_verify::CheckAudience;
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
  // A valid error for a RequireAny
  Status status_;
};

class ContextImpl : public Verifier::Context {
public:
  ContextImpl(Http::RequestHeaderMap& headers, Tracing::Span& parent_span,
              Verifier::Callbacks& callback)
      : headers_(headers), parent_span_(parent_span), callback_(callback) {}

  Http::RequestHeaderMap& headers() const override { return headers_; }

  Tracing::Span& parentSpan() const override { return parent_span_; }

  Verifier::Callbacks* callback() const override { return &callback_; }

  void cancel() override {
    for (const auto& it : auths_) {
      it->onDestroy();
    }
  }

  // Get Response data which can be used to check if a verifier node has responded or not.
  CompletionState& getCompletionState(const Verifier* verifier) {
    return completion_states_[verifier];
  }

  // Stores an authenticator object for this request.
  void storeAuth(AuthenticatorPtr&& auth) { auths_.emplace_back(std::move(auth)); }

  // Add a pair of (name, payload), called by Authenticator. It can be either JWT header or payload.
  void addExtractedData(const std::string& name, const ProtobufWkt::Struct& extracted_data) {
    *(*extracted_data_.mutable_fields())[name].mutable_struct_value() = extracted_data;
  }

  void setExtractedData() {
    if (!extracted_data_.fields().empty()) {
      callback_.setExtractedData(extracted_data_);
    }
  }

private:
  Http::RequestHeaderMap& headers_;
  Tracing::Span& parent_span_;
  Verifier::Callbacks& callback_;
  absl::node_hash_map<const Verifier*, CompletionState> completion_states_;
  std::vector<AuthenticatorPtr> auths_;
  ProtobufWkt::Struct extracted_data_;
};

// base verifier for provider_name, provider_and_audiences, and allow_missing_or_failed.
class BaseVerifierImpl : public Logger::Loggable<Logger::Id::jwt>, public Verifier {
public:
  BaseVerifierImpl(const BaseVerifierImpl* parent) : parent_(parent) {}

  void completeWithStatus(Status status, ContextImpl& context) const {
    if (parent_ != nullptr) {
      auto& completion_state = context.getCompletionState(this);
      completion_state.status_ = status;
      return parent_->onComplete(status, context);
    }
    context.setExtractedData();
    context.callback()->onComplete(status);
    context.cancel();
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
  ProviderVerifierImpl(const std::string& provider_name, const AuthFactory& factory,
                       const JwtProvider& provider, const BaseVerifierImpl* parent)
      : BaseVerifierImpl(parent), auth_factory_(factory), extractor_(Extractor::create(provider)),
        provider_name_(provider_name) {}

  void verify(ContextSharedPtr context) const override {
    auto& ctximpl = static_cast<ContextImpl&>(*context);
    auto auth = auth_factory_.create(getAudienceChecker(), provider_name_, false, false);
    extractor_->sanitizeHeaders(ctximpl.headers());
    auth->verify(
        ctximpl.headers(), ctximpl.parentSpan(), extractor_->extract(ctximpl.headers()),
        [&ctximpl](const std::string& name, const ProtobufWkt::Struct& extracted_data) {
          ctximpl.addExtractedData(name, extracted_data);
        },
        [this, &ctximpl](const Status& status) { onComplete(status, ctximpl); },
        [&ctximpl]() { ctximpl.callback()->clearRouteCache(); });
    if (!ctximpl.getCompletionState(this).is_completed_) {
      ctximpl.storeAuth(std::move(auth));
    } else {
      auth->onDestroy();
    }
  }

protected:
  virtual const CheckAudience* getAudienceChecker() const { return nullptr; }

private:
  const AuthFactory& auth_factory_;
  const ExtractorConstPtr extractor_;
  const std::string provider_name_;
};

class ProviderAndAudienceVerifierImpl : public ProviderVerifierImpl {
public:
  ProviderAndAudienceVerifierImpl(const std::string& provider_name, const AuthFactory& factory,
                                  const JwtProvider& provider, const BaseVerifierImpl* parent,
                                  const std::vector<std::string>& config_audiences)
      : ProviderVerifierImpl(provider_name, factory, provider, parent),
        check_audience_(std::make_unique<CheckAudience>(config_audiences)) {}

private:
  const CheckAudience* getAudienceChecker() const override { return check_audience_.get(); }

  // Check audience object
  ::google::jwt_verify::CheckAudiencePtr check_audience_;
};

// Allow missing or failed verifier
class AllowFailedVerifierImpl : public BaseVerifierImpl {
public:
  AllowFailedVerifierImpl(const AuthFactory& factory, const JwtProviderList& providers,
                          const BaseVerifierImpl* parent)
      : BaseVerifierImpl(parent), auth_factory_(factory), extractor_(Extractor::create(providers)) {
  }

  void verify(ContextSharedPtr context) const override {
    auto& ctximpl = static_cast<ContextImpl&>(*context);
    auto auth = auth_factory_.create(nullptr, absl::nullopt, true, true);
    extractor_->sanitizeHeaders(ctximpl.headers());
    auth->verify(
        ctximpl.headers(), ctximpl.parentSpan(), extractor_->extract(ctximpl.headers()),
        [&ctximpl](const std::string& name, const ProtobufWkt::Struct& extracted_data) {
          ctximpl.addExtractedData(name, extracted_data);
        },
        [this, &ctximpl](const Status& status) { onComplete(status, ctximpl); },
        [&ctximpl]() { ctximpl.callback()->clearRouteCache(); });
    if (!ctximpl.getCompletionState(this).is_completed_) {
      ctximpl.storeAuth(std::move(auth));
    } else {
      auth->onDestroy();
    }
  }

private:
  const AuthFactory& auth_factory_;
  const ExtractorConstPtr extractor_;
};

class AllowMissingVerifierImpl : public BaseVerifierImpl {
public:
  AllowMissingVerifierImpl(const AuthFactory& factory, const JwtProviderList& providers,
                           const BaseVerifierImpl* parent)
      : BaseVerifierImpl(parent), auth_factory_(factory), extractor_(Extractor::create(providers)) {
  }

  void verify(ContextSharedPtr context) const override {
    ENVOY_LOG(debug, "Called AllowMissingVerifierImpl.verify : {}", __func__);

    auto& ctximpl = static_cast<ContextImpl&>(*context);
    auto auth = auth_factory_.create(nullptr, absl::nullopt, false /* allow failed */,
                                     true /* allow missing */);
    extractor_->sanitizeHeaders(ctximpl.headers());
    auth->verify(
        ctximpl.headers(), ctximpl.parentSpan(), extractor_->extract(ctximpl.headers()),
        [&ctximpl](const std::string& name, const ProtobufWkt::Struct& extracted_data) {
          ctximpl.addExtractedData(name, extracted_data);
        },
        [this, &ctximpl](const Status& status) { onComplete(status, ctximpl); },
        [&ctximpl]() { ctximpl.callback()->clearRouteCache(); });
    if (!ctximpl.getCompletionState(this).is_completed_) {
      ctximpl.storeAuth(std::move(auth));
    } else {
      auth->onDestroy();
    }
  }

private:
  const AuthFactory& auth_factory_;
  const ExtractorConstPtr extractor_;
};

VerifierConstPtr innerCreate(const JwtRequirement& requirement,
                             const Protobuf::Map<std::string, JwtProvider>& providers,
                             const AuthFactory& factory, const BaseVerifierImpl* parent);

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
  std::vector<VerifierConstPtr> verifiers_;
};

// Requires any verifier.
class AnyVerifierImpl : public BaseGroupVerifierImpl {
public:
  AnyVerifierImpl(const JwtRequirementOrList& or_list, const AuthFactory& factory,
                  const Protobuf::Map<std::string, JwtProvider>& providers,
                  const BaseVerifierImpl* parent)
      : BaseGroupVerifierImpl(parent) {

    for (const auto& it : or_list.requirements()) {
      switch (it.requires_type_case()) {
      case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
        is_allow_missing_or_failed_ = true;
        break;
      case JwtRequirement::RequiresTypeCase::kAllowMissing:
        is_allow_missing_ = true;
        break;
      default:
        verifiers_.emplace_back(innerCreate(it, providers, factory, this));
        break;
      }
    }

    // RequiresAny only has one missing or failed requirement.
    if (verifiers_.empty() && (is_allow_missing_or_failed_ || is_allow_missing_)) {
      JwtRequirement requirement;
      if (is_allow_missing_or_failed_) {
        requirement.mutable_allow_missing_or_failed();
      } else {
        requirement.mutable_allow_missing();
      }
      verifiers_.emplace_back(innerCreate(requirement, providers, factory, this));
    }
  }

  void onComplete(const Status& status, ContextImpl& context) const override {
    auto& completion_state = context.getCompletionState(this);
    if (completion_state.is_completed_) {
      return;
    }

    // If any of children is OK, return OK
    if (Status::Ok == status) {
      completion_state.is_completed_ = true;
      completeWithStatus(status, context);
      return;
    }

    // Then wait for all children to be done.
    if (++completion_state.number_completed_children_ == verifiers_.size()) {
      // Aggregate all children status into a final status.
      // JwtMissed and JwtUnknownIssuer should be treated differently than other errors.
      // JwtMissed means not Jwt token for the required provider.
      // JwtUnknownIssuer means wrong issuer for the required provider.
      Status final_status = Status::JwtMissed;
      for (const auto& it : verifiers_) {
        // Prefer errors which are not JwtMissed nor JwtUnknownIssuer.
        // Prefer JwtUnknownIssuer between JwtMissed and JwtUnknownIssuer.
        Status child_status = context.getCompletionState(it.get()).status_;
        if ((child_status != Status::JwtMissed && child_status != Status::JwtUnknownIssuer) ||
            final_status == Status::JwtMissed) {
          final_status = child_status;
        }
      }

      if (is_allow_missing_or_failed_) {
        final_status = Status::Ok;
      } else if (is_allow_missing_ && final_status == Status::JwtMissed) {
        final_status = Status::Ok;
      }
      completion_state.is_completed_ = true;
      completeWithStatus(final_status, context);
    }
  }

private:
  bool is_allow_missing_or_failed_{false};
  bool is_allow_missing_{false};
};

// Requires all verifier
class AllVerifierImpl : public BaseGroupVerifierImpl {
public:
  AllVerifierImpl(const JwtRequirementAndList& and_list, const AuthFactory& factory,
                  const Protobuf::Map<std::string, JwtProvider>& providers,
                  // const Extractor& extractor_for_allow_fail,
                  const BaseVerifierImpl* parent)
      : BaseGroupVerifierImpl(parent) {
    for (const auto& it : and_list.requirements()) {
      verifiers_.emplace_back(innerCreate(it, providers, factory, this));
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
    completeWithStatus(Status::Ok, static_cast<ContextImpl&>(*context));
  }
};

JwtProviderList getAllProvidersAsList(const Protobuf::Map<std::string, JwtProvider>& providers) {
  JwtProviderList list;
  for (const auto& it : providers) {
    list.emplace_back(&it.second);
  }
  return list;
}

VerifierConstPtr innerCreate(const JwtRequirement& requirement,
                             const Protobuf::Map<std::string, JwtProvider>& providers,
                             const AuthFactory& factory, const BaseVerifierImpl* parent) {
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
                                             parent);
  case JwtRequirement::RequiresTypeCase::kRequiresAll:
    return std::make_unique<AllVerifierImpl>(requirement.requires_all(), factory, providers,
                                             parent);
  case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
    return std::make_unique<AllowFailedVerifierImpl>(factory, getAllProvidersAsList(providers),
                                                     parent);
  case JwtRequirement::RequiresTypeCase::kAllowMissing:
    return std::make_unique<AllowMissingVerifierImpl>(factory, getAllProvidersAsList(providers),
                                                      parent);
  case JwtRequirement::RequiresTypeCase::REQUIRES_TYPE_NOT_SET:
    return std::make_unique<AllowAllVerifierImpl>(parent);
  }

  const auto& it = providers.find(provider_name);
  if (it == providers.end()) {
    throw EnvoyException(fmt::format("Required provider ['{}'] is not configured.", provider_name));
  }
  if (audiences.empty()) {
    return std::make_unique<ProviderVerifierImpl>(provider_name, factory, it->second, parent);
  }
  return std::make_unique<ProviderAndAudienceVerifierImpl>(provider_name, factory, it->second,
                                                           parent, audiences);
}

} // namespace

ContextSharedPtr Verifier::createContext(Http::RequestHeaderMap& headers,
                                         Tracing::Span& parent_span, Callbacks* callback) {
  return std::make_shared<ContextImpl>(headers, parent_span, *callback);
}

VerifierConstPtr Verifier::create(const JwtRequirement& requirement,
                                  const Protobuf::Map<std::string, JwtProvider>& providers,
                                  const AuthFactory& factory) {
  return innerCreate(requirement, providers, factory, nullptr);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
