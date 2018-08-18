#pragma once

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"
#include "envoy/http/header_map.h"

#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "jwt_verify_lib/verify.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Matcher;
typedef std::shared_ptr<const Matcher> MatcherConstSharedPtr;

class AsyncMatcher;
typedef std::shared_ptr<AsyncMatcher> AsyncMatcherSharedPtr;

/**
 * Supports matching a HTTP requests with JWT requirements.
 */
class Matcher {
public:
  virtual ~Matcher() {}

  /**
   * Returns if a HTTP request matches with the rules of the matcher.
   *
   * @param headers    the request headers used to match against. An empty map should be used if
   *                   there are none headers available.
   * @return  true if request is a match, false otherwise.
   */
  virtual bool matches(const Http::HeaderMap& headers) const PURE;

  /**
   * Factory method to create a shared instance of a matcher based on the rule defined.
   *
   * @param route  the proto route match message.
   * @return the matcher instance.
   */
  static MatcherConstSharedPtr create(const ::envoy::api::v2::route::RouteMatch& route);
};

class AsyncMatcher {
public:
  virtual ~AsyncMatcher() {}

  class Callbacks {
  public:
    virtual ~Callbacks() {}
    virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
  };

  virtual void matches(Http::HeaderMap& headers, Callbacks& callback) PURE;
  virtual void close() PURE;

  static AsyncMatcherSharedPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement& requirement,
         FilterConfigSharedPtr config);

  static AsyncMatcherSharedPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule& rule,
         FilterConfigSharedPtr config);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
