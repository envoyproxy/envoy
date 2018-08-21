#include "extensions/filters/http/jwt_authn/verifier.h"

#include "common/common/enum_to_int.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementAndList;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementOrList;
using ::Envoy::Http::LowerCaseString;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

void AuthVerifier::beforeVerify(const std::vector<std::string>& audiences, Http::HeaderMap& headers,
                                Verifier::Callbacks& callback) {
  auth_ = factory_(audiences);
  callback_ = &callback;
  // Remove headers configured to pass payload
  auth_->sanitizePayloadHeaders(headers);
}

void AuthVerifier::close() {
  if (auth_) {
    auth_->onDestroy();
  }
}

ProviderNameVerifier::ProviderNameVerifier(
    const std::string& provider_name, const std::vector<std::string>& audiences,
    const AuthFactory& factory, const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers)
    : AuthVerifier(factory), audiences_(audiences) {
  const auto& it = providers.find(provider_name);
  if (it == providers.end()) {
    throw EnvoyException(fmt::format("Required provider ['{}'] is not configured.", provider_name));
  }
  for (const auto& header : it->second.from_headers()) {
    extract_param_.header_keys_.insert(LowerCaseString(header.name()).get() +
                                       header.value_prefix());
  }
  for (const auto& param : it->second.from_params()) {
    extract_param_.param_keys_.insert(param);
  }
  if (it->second.from_headers_size() == 0 && it->second.from_params_size() == 0) {
    extract_param_.header_keys_.insert("authorizationBearer ");
  }
  issuer_ = it->second.issuer();
}

void ProviderNameVerifier::verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) {
  beforeVerify(audiences_, headers, callback);

  auth_->verify(&extract_param_, absl::optional<std::string>{issuer_}, headers, this);
}

void ProviderNameVerifier::onComplete(const Status& status) {
  if (callback_) {
    callback_->onComplete(status);
    callback_ = nullptr;
    close();
  }
}

void AllowFailedVerifier::verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) {
  beforeVerify({}, headers, callback);

  auth_->verify(nullptr, absl::nullopt, headers, this);
}

void AllowFailedVerifier::onComplete(const Status&) {
  if (callback_) {
    callback_->onComplete(Status::Ok);
    callback_ = nullptr;
    close();
  }
}

void GroupVerifier::verify(Http::HeaderMap& headers, Verifier::Callbacks& callback) {
  callback_ = &callback;
  count_ = 0;
  for (const auto& it : verifiers_) {
    if (callback_) {
      it->verify(headers, *this);
    }
  }
}

void GroupVerifier::close() {
  for (const auto& it : verifiers_) {
    it->close();
  }
}

AnyVerifier::AnyVerifier(const JwtRequirementOrList& or_list, const AuthFactory& factory,
                         const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers) {
  for (const auto& it : or_list.requirements()) {
    verifiers_.push_back(Verifier::create(it, providers, factory));
  }
}

void AnyVerifier::onComplete(const ::google::jwt_verify::Status& status) {
  if (!callback_) {
    return;
  }
  if (++count_ == verifiers_.size() || Status::Ok == status) {
    callback_->onComplete(status);
    callback_ = nullptr;
  }
}

AllVerifier::AllVerifier(const JwtRequirementAndList& and_list, const AuthFactory& factory,
                         const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers) {
  for (const auto& it : and_list.requirements()) {
    verifiers_.push_back(Verifier::create(it, providers, factory));
  }
}

void AllVerifier::onComplete(const ::google::jwt_verify::Status& status) {
  if (!callback_) {
    return;
  }
  if (++count_ == verifiers_.size() || Status::Ok != status) {
    callback_->onComplete(status);
    callback_ = nullptr;
  }
}

VerifierPtr Verifier::create(const JwtRequirement& requirement,
                             const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                             const AuthFactory& factory) {
  switch (requirement.requires_type_case()) {
  case JwtRequirement::RequiresTypeCase::kProviderName:
    return std::make_unique<ProviderNameVerifier>(requirement.provider_name(),
                                                  std::vector<std::string>{}, factory, providers);
  case JwtRequirement::RequiresTypeCase::kProviderAndAudiences: {
    std::vector<std::string> audiences;
    for (const auto& it : requirement.provider_and_audiences().audiences()) {
      audiences.emplace_back(it);
    }
    return std::make_unique<ProviderNameVerifier>(
        requirement.provider_and_audiences().provider_name(), audiences, factory, providers);
  }
  case JwtRequirement::RequiresTypeCase::kRequiresAny:
    return std::make_unique<AnyVerifier>(requirement.requires_any(), factory, providers);
  case JwtRequirement::RequiresTypeCase::kRequiresAll:
    return std::make_unique<AllVerifier>(requirement.requires_all(), factory, providers);
  case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
    return std::make_unique<AllowFailedVerifier>(factory);
  case JwtRequirement::RequiresTypeCase::REQUIRES_TYPE_NOT_SET:
    return std::make_unique<AllowAllVerifier>();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
