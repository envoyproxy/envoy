#include "extensions/filters/http/jwt_authn/requirement_matchers.h"

#include "common/common/enum_to_int.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementAndList;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementOrList;
using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;
using ::Envoy::Http::LowerCaseString;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

void SingleMatcher::close() {
  if (auth_ != nullptr) {
    auth_->onDestroy();
  }
}

ProviderNameMatcher::ProviderNameMatcher(std::string provider_name, FilterConfigSharedPtr config,
                                         AuthenticatorPtr&& auth)
    : SingleMatcher(std::move(auth)) {
  const auto& map = config->getProtoConfig().providers();
  const auto& it = map.find(provider_name);
  if (it == map.end()) {
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

void ProviderNameMatcher::matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) {
  callback_ = &callback;
  // Remove headers configured to pass payload
  auth_->sanitizePayloadHeaders(headers);

  auth_->verify(&extract_param_, absl::optional<std::string>{issuer_}, headers, this);
}

void ProviderNameMatcher::onComplete(const Status& status) {
  if (callback_) {
    callback_->onComplete(status);
    callback_ = nullptr;
  }
}

void AllowFailedMatcher::matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) {
  callback_ = &callback;
  // Remove headers configured to pass payload
  auth_->sanitizePayloadHeaders(headers);

  auth_->verify(nullptr, absl::nullopt, headers, this);
}

void AllowFailedMatcher::onComplete(const Status&) {
  if (callback_) {
    callback_->onComplete(Status::Ok);
    callback_ = nullptr;
  }
}

void GroupMatcher::matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) {
  callback_ = &callback;
  {
    Thread::LockGuard lock(lock_);
    count_ = 0;
  }
  for (const auto& it : matchers_) {
    it->matches(headers, *this);
  }
}

void GroupMatcher::close() {
  for (const auto& it : matchers_) {
    it->close();
  }
}

AnyMatcher::AnyMatcher(const JwtRequirementOrList& or_list, FilterConfigSharedPtr config) {
  for (const auto& it : or_list.requirements()) {
    matchers_.push_back(AsyncMatcher::create(it, config));
  }
}

void AnyMatcher::onComplete(const ::google::jwt_verify::Status& status) {
  if (!callback_) {
    return;
  }
  bool done = false;
  {
    Thread::LockGuard lock(lock_);
    done = (++count_ == matchers_.size()) || Status::Ok == status;
  }
  if (done) {
    callback_->onComplete(status);
    callback_ = nullptr;
  }
}

AllMatcher::AllMatcher(const JwtRequirementAndList& and_list, FilterConfigSharedPtr config) {
  for (const auto& it : and_list.requirements()) {
    matchers_.push_back(AsyncMatcher::create(it, config));
  }
}

void AllMatcher::onComplete(const ::google::jwt_verify::Status& status) {
  if (!callback_) {
    return;
  }
  bool done = false;
  {
    Thread::LockGuard lock(lock_);
    done = (++count_ == matchers_.size()) || Status::Ok != status;
  }
  if (done) {
    callback_->onComplete(status);
    callback_ = nullptr;
  }
}

RuleMatcher::RuleMatcher(const RequirementRule& rule, FilterConfigSharedPtr config) {
  route_matcher_ = Matcher::create(rule.match());
  requirement_matcher_ = AsyncMatcher::create(rule.requires(), config);
}

void RuleMatcher::matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) {
  callback_ = &callback;
  if (route_matcher_->matches(headers)) {
    requirement_matcher_->matches(headers, *this);
    return;
  }
  onComplete(Status::Ok);
}

void RuleMatcher::onComplete(const ::google::jwt_verify::Status& status) {
  callback_->onComplete(status);
  callback_ = nullptr;
}

void RuleMatcher::close() { requirement_matcher_->close(); }

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
