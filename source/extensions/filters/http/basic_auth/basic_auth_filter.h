#pragma once

#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

/**
 * All Basic Auth filter stats. @see stats_macros.h
 */
#define ALL_BASIC_AUTH_STATS(COUNTER)                                                              \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)

/**
 * Struct definition for Basic Auth stats. @see stats_macros.h
 */
struct BasicAuthStats {
  ALL_BASIC_AUTH_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Struct definition for username password pairs.
 */
struct User {
  // the user name
  std::string name;
  // the hashed password,  see https://httpd.apache.org/docs/2.4/misc/password_encryptions.html
  std::string hash;
};

using UserMap = absl::flat_hash_map<std::string, User>;

/**
 * Configuration for the Basic Auth filter.
 */
class FilterConfig {
public:
  FilterConfig(UserMap&& users, const std::string& forward_username_header,
               const std::string& authentication_header, const std::string& stats_prefix,
               Stats::Scope& scope);
  const BasicAuthStats& stats() const { return stats_; }
  const std::string& forwardUsernameHeader() const { return forward_username_header_; }
  const UserMap& users() const { return users_; }
  const Http::LowerCaseString& authenticationHeader() const { return authentication_header_; }

private:
  static BasicAuthStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return BasicAuthStats{ALL_BASIC_AUTH_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  const UserMap users_;
  const std::string forward_username_header_;
  const Http::LowerCaseString authentication_header_;
  BasicAuthStats stats_;
};
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Per route settings for BasicAuth. Allows customizing users on a virtualhost\route\weighted
 * cluster level.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(UserMap&& users) : users_(std::move(users)) {}
  const UserMap& users() const { return users_; }

private:
  const UserMap users_;
};

// The Envoy filter to process HTTP basic auth.
class BasicAuthFilter : public Http::PassThroughDecoderFilter,
                        public Logger::Loggable<Logger::Id::basic_auth> {
public:
  BasicAuthFilter(FilterConfigConstSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  bool validateUser(const UserMap& users, absl::string_view username,
                    absl::string_view password) const;

private:
  Http::FilterHeadersStatus onDenied(absl::string_view body,
                                     absl::string_view response_code_details);

  // The callback function.
  FilterConfigConstSharedPtr config_;
};

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
