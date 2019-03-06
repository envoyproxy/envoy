#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Http {

class CodeStatsImpl : public CodeStats {
public:
  ~CodeStatsImpl() override = default;

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                               Code response_code) const override;
  void chargeResponseStat(const ResponseStatInfo& info) const override;
  void chargeResponseTiming(const ResponseTimingInfo& info) const override;

private:
  friend class CodeStatsTest;

  /**
   * Strips any trailing "." from a prefix. This is handy as most prefixes
   * are specified as a literal like "http.", or an empty-string "". We
   * are going to be passing these, as well as other tokens, to join() below,
   * which will add "." between each token.
   *
   * TODO(jmarantz): remove all the trailing dots in all stat prefix string
   * literals. Then this function can be removed.
   */
  static absl::string_view stripTrailingDot(absl::string_view prefix);

  /**
   * Joins a string-view vector with "." between each token. If there's an
   * initial blank token it is skipped. Leading blank tokens occur due to empty
   * prefixes, which are fairly common.
   *
   * Note: this layer probably should be called something other than join(),
   * like joinSkippingLeadingEmptyToken but I thought the decrease in
   * readability at all the call-sites would not be worth it.
   */
  static std::string join(const std::vector<absl::string_view>& v);

  // Predeclared tokens used for combining with join().
  const absl::string_view canary_upstream_rq_completed_{"canary.upstream_rq_completed"};
  const absl::string_view canary_upstream_rq_time_{"canary.upstream_rq_time"};
  const absl::string_view canary_upstream_rq_{"canary.upstream_rq_"};
  const absl::string_view external_rq_time_{"external.upstream_rq_time"};
  const absl::string_view external_upstream_rq_completed_{"external.upstream_rq_completed"};
  const absl::string_view external_upstream_rq_time_{"external.upstream_rq_time"};
  const absl::string_view external_upstream_rq_{"external.upstream_rq_"};
  const absl::string_view internal_rq_time_{"internal.upstream_rq_time"};
  const absl::string_view internal_upstream_rq_completed_{"internal.upstream_rq_completed"};
  const absl::string_view internal_upstream_rq_time_{"internal.upstream_rq_time"};
  const absl::string_view internal_upstream_rq_{"internal.upstream_rq_"};
  const absl::string_view upstream_rq_completed_{"upstream_rq_completed"};
  const absl::string_view upstream_rq_time_{"upstream_rq_time"};
  const absl::string_view upstream_rq_{"upstream_rq_"};
  const absl::string_view vcluster_{"vcluster"};
  const absl::string_view vhost_{"vhost"};
  const absl::string_view zone_{"zone"};
};

/**
 * General utility routines for HTTP codes.
 */
class CodeUtility {
public:
  /**
   * Convert an HTTP response code to a descriptive string.
   * @param code supplies the code to convert.
   * @return const char* the string.
   */
  static const char* toString(Code code);

  static bool is1xx(uint64_t code) { return code >= 100 && code < 200; }
  static bool is2xx(uint64_t code) { return code >= 200 && code < 300; }
  static bool is3xx(uint64_t code) { return code >= 300 && code < 400; }
  static bool is4xx(uint64_t code) { return code >= 400 && code < 500; }
  static bool is5xx(uint64_t code) { return code >= 500 && code < 600; }

  static bool isGatewayError(uint64_t code) { return code >= 502 && code < 505; }

  static std::string groupStringForResponseCode(Code response_code);
};

} // namespace Http
} // namespace Envoy
