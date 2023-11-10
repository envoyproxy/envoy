#pragma once

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
  FilterConfig(UserMap&& users, const std::string& stats_prefix, Stats::Scope& scope);
  const BasicAuthStats& stats() const { return stats_; }
  bool validateUser(absl::string_view username, absl::string_view password) const;

private:
  static BasicAuthStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return BasicAuthStats{ALL_BASIC_AUTH_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  const UserMap users_;
  BasicAuthStats stats_;
};
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;

// The Envoy filter to process HTTP basic auth.
class BasicAuthFilter : public Http::PassThroughDecoderFilter,
                        public Logger::Loggable<Logger::Id::basic_auth> {
public:
  BasicAuthFilter(FilterConfigConstSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

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
