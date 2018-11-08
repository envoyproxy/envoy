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
  CodeStatsImpl();
  ~CodeStatsImpl() override;

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                               Code response_code) override;
  void chargeResponseStat(const ResponseStatInfo& info) override;
  void chargeResponseTiming(const ResponseTimingInfo& info) override;

  static std::string join(const std::vector<absl::string_view>& v);

private:
  absl::string_view stripTrailingDot(absl::string_view prefix);

  absl::string_view canary_upstream_rq_completed_{"canary.upstream_rq_completed"};
  absl::string_view canary_upstream_rq_time_{"canary.upstream_rq_time"};
  absl::string_view canary_upstream_rq_{"canary.upstream_rq_"};
  absl::string_view external_rq_time_{"external.upstream_rq_time"};
  absl::string_view external_upstream_rq_completed_{"external.upstream_rq_completed"};
  absl::string_view external_upstream_rq_time_{"external.upstream_rq_time"};
  absl::string_view external_upstream_rq_{"external.upstream_rq_"};
  absl::string_view internal_rq_time_{"internal.upstream_rq_time"};
  absl::string_view internal_upstream_rq_completed_{"internal.upstream_rq_completed"};
  absl::string_view internal_upstream_rq_time_{"internal.upstream_rq_time"};
  absl::string_view internal_upstream_rq_{"internal.upstream_rq_"};
  absl::string_view upstream_rq_completed_{"upstream_rq_completed"};
  absl::string_view upstream_rq_time_{"upstream_rq_time"};
  absl::string_view upstream_rq_time{"upstream_rq_time"};
  absl::string_view upstream_rq_{"upstream_rq_"};
  absl::string_view vcluster_{"vcluster"};
  absl::string_view vhost_{"vhost"};
  absl::string_view zone_{"zone"};
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
