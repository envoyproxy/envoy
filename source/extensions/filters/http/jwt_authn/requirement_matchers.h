#pragma once

#include "envoy/http/async_client.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// base matcher for provider_name, provider_and_audiences, and allow_missing_or_failed.
class SingleMatcher : public AsyncMatcher, public Authenticator::Callbacks {
public:
  SingleMatcher(AuthenticatorPtr&& auth) : auth_(std::move(auth)) {}
  void close() override;

protected:
  // The Authenticator object.
  AuthenticatorPtr auth_;
  // The caller's callback.
  AsyncMatcher::Callbacks* callback_;
};

class ProviderNameMatcher : public SingleMatcher {
public:
  ProviderNameMatcher(std::string provider_name, FilterConfigSharedPtr config,
                      AuthenticatorPtr&& auth);
  void matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) override;
  void onComplete(const ::google::jwt_verify::Status& status) override;

private:
  // provider token location info
  ExtractParam extract_param_;
  std::string issuer_;
};

// allow missing or failed matcher
class AllowFailedMatcher : public SingleMatcher {
public:
  AllowFailedMatcher(AuthenticatorPtr auth) : SingleMatcher(std::move(auth)) {}

  void matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) override;
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// Base Matcher for requires all or any.
class GroupMatcher : public AsyncMatcher, public AsyncMatcher::Callbacks {
public:
  void matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) override;
  void close() override;

protected:
  // The list of requirement matchers
  std::vector<AsyncMatcherSharedPtr> matchers_;
  // Mutex object.
  Thread::MutexBasicLockable lock_;
  // Current return count
  std::size_t count_ GUARDED_BY(lock_);
  // The caller's callback.
  AsyncMatcher::Callbacks* callback_;
};

// requires any matcher.
class AnyMatcher : public GroupMatcher {
public:
  AnyMatcher(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementOrList& or_list,
             FilterConfigSharedPtr config);
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// requires all matcher
class AllMatcher : public GroupMatcher {
public:
  AllMatcher(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirementAndList& and_list,
      FilterConfigSharedPtr config);
  void onComplete(const ::google::jwt_verify::Status& status) override;
};

// matcher for checking route and requirement.
class RuleMatcher : public AsyncMatcher, public AsyncMatcher::Callbacks {
public:
  RuleMatcher(const ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule& rule,
              FilterConfigSharedPtr config);
  void matches(Http::HeaderMap& headers, AsyncMatcher::Callbacks& callback) override;
  void onComplete(const ::google::jwt_verify::Status& status) override;
  void close() override;

private:
  // The caller's callback
  AsyncMatcher::Callbacks* callback_;
  // The route matcher object.
  MatcherConstSharedPtr route_matcher_;
  // The requirement matcher.
  AsyncMatcherSharedPtr requirement_matcher_;
};

// match all, for requirement not set
class AllowAllMatcher : public AsyncMatcher {
public:
  void matches(Http::HeaderMap&, AsyncMatcher::Callbacks& callback) override {
    callback.onComplete(::google::jwt_verify::Status::Ok);
  }

  void close() override {}
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
