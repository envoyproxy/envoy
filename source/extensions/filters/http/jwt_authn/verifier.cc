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

// base verifier for provider_name, provider_and_audiences, and allow_missing_or_failed.
class BaseVerifierImpl : public Verifier {
public:
  void registerParent(Verifier* parent) override { parent_ = parent; }

  void onCompleteHelper(Status status, VerifyContext& context) {
    if (parent_ != nullptr) {
      return parent_->onComplete(status, context);
    }
    return context.callback()->onComplete(status);
  }

  virtual void onComplete(const Status& status, VerifyContext& context) override {
    if (!context.hasResponded(this)) {
      context.setResponded(this);
      onCompleteHelper(status, context);
    }
  }

protected:
  // The parent group verifier.
  Verifier* parent_{};
};

// Provider specific verifier
class ProviderVerifierImpl : public BaseVerifierImpl {
public:
  ProviderVerifierImpl(const std::vector<std::string>& audiences, const AuthFactory& factory,
                       const JwtProvider& provider)
      : audiences_(audiences), auth_factory_(factory),
        extractor_(
            Extractor::create(provider.issuer(), provider.from_headers(), provider.from_params())),
        issuer_(provider.issuer()) {}

  void verify(VerifyContext& context) override {
    auto auth = auth_factory_.create(audiences_, absl::optional<std::string>{issuer_}, false);
    auth->sanitizePayloadHeaders(context.headers());
    auth->verify(context.headers(), extractor_->extract(context.headers()),
                 [&](const Status& status) { onComplete(status, context); });
    if (!context.hasResponded(this)) {
      context.addAuth(std::move(auth));
    }
  }

private:
  const std::vector<std::string> audiences_;
  const AuthFactory& auth_factory_;
  const ExtractorConstPtr extractor_;
  const std::string issuer_;
};

// Allow missing or failed verifier
class AllowFailedVerifierImpl : public BaseVerifierImpl {
public:
  AllowFailedVerifierImpl(const AuthFactory& factory, const Extractor& extractor, bool allow_failed)
      : auth_factory_(factory), extractor_(extractor), allow_failed_(allow_failed) {}

  void verify(VerifyContext& context) override {
    auto auth = auth_factory_.create({}, absl::nullopt, allow_failed_);
    auth->sanitizePayloadHeaders(context.headers());
    auth->verify(context.headers(), extractor_.extract(context.headers()),
                 [&](const Status& status) { onComplete(status, context); });
    if (!context.hasResponded(this)) {
      context.addAuth(std::move(auth));
    }
  }

private:
  const AuthFactory& auth_factory_;
  const Extractor& extractor_;
  const bool allow_failed_;
};

// Base verifier for requires all or any.
class BaseGroupVerifierImpl : public BaseVerifierImpl {
public:
  void verify(VerifyContext& context) override {
    for (const auto& it : verifiers_) {
      if (!context.hasResponded(this)) {
        it->verify(context);
      }
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
                  const Extractor& extractor) {
    for (const auto& it : or_list.requirements()) {
      auto verifier = Verifier::create(it, providers, factory, extractor);
      verifier->registerParent(this);
      verifiers_.push_back(std::move(verifier));
    }
  }

  void onComplete(const Status& status, VerifyContext& context) override {
    if (context.hasResponded(this)) {
      return;
    }
    if (context.incrementAndGetCount(this) == verifiers_.size() || Status::Ok == status) {
      context.setResponded(this);
      onCompleteHelper(status, context);
    }
  }
};

// Requires all verifier
class AllVerifierImpl : public BaseGroupVerifierImpl {
public:
  AllVerifierImpl(const JwtRequirementAndList& and_list, const AuthFactory& factory,
                  const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                  const Extractor& extractor) {
    for (const auto& it : and_list.requirements()) {
      auto verifier = Verifier::create(it, providers, factory, extractor);
      verifier->registerParent(this);
      verifiers_.push_back(std::move(verifier));
    }
  }

  void onComplete(const Status& status, VerifyContext& context) override {
    if (context.hasResponded(this)) {
      return;
    }
    if (context.incrementAndGetCount(this) == verifiers_.size() || Status::Ok != status) {
      context.setResponded(this);
      onCompleteHelper(status, context);
    }
  }
};

// Match all, for requirement not set
class AllowAllVerifierImpl : public BaseVerifierImpl {
public:
  void verify(VerifyContext& context) override { onComplete(Status::Ok, context); }

  void onComplete(const Status& status, VerifyContext& context) override {
    onCompleteHelper(status, context);
  }
};

} // namespace

VerifierPtr Verifier::create(const JwtRequirement& requirement,
                             const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                             const AuthFactory& factory, const Extractor& extractor) {
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
                                             extractor);
  case JwtRequirement::RequiresTypeCase::kRequiresAll:
    return std::make_unique<AllVerifierImpl>(requirement.requires_all(), factory, providers,
                                             extractor);
  case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
    return std::make_unique<AllowFailedVerifierImpl>(factory, extractor,
                                                     requirement.allow_missing_or_failed().value());
  case JwtRequirement::RequiresTypeCase::REQUIRES_TYPE_NOT_SET:
    return std::make_unique<AllowAllVerifierImpl>();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const auto& it = providers.find(provider_name);
  if (it == providers.end()) {
    throw EnvoyException(fmt::format("Required provider ['{}'] is not configured.", provider_name));
  }
  return std::make_unique<ProviderVerifierImpl>(audiences, factory, it->second);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
