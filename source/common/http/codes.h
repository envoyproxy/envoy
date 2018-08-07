#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Http {

/**
 * General utility routines for HTTP codes.
 */
class CodeUtility {
public:
  /**
   * Charge a simple response stat to an upstream.
   */
  static void chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                                      Code response_code);

  struct ResponseStatInfo {
    Stats::Scope& global_scope_;
    Stats::Scope& cluster_scope_;
    const std::string& prefix_;
    uint64_t response_status_code_;
    bool internal_request_;
    const std::string& request_vhost_name_;
    const std::string& request_vcluster_name_;
    const std::string& from_zone_;
    const std::string& to_zone_;
    bool upstream_canary_;
  };

  /**
   * Charge a response stat to both agg counters (*xx) as well as code specific counters. This
   * routine also looks for the x-envoy-upstream-canary header and if it is set, also charges
   * canary stats.
   */
  static void chargeResponseStat(const ResponseStatInfo& info);

  struct ResponseTimingInfo {
    Stats::Scope& global_scope_;
    Stats::Scope& cluster_scope_;
    const std::string& prefix_;
    std::chrono::milliseconds response_time_;
    bool upstream_canary_;
    bool internal_request_;
    const std::string& request_vhost_name_;
    const std::string& request_vcluster_name_;
    const std::string& from_zone_;
    const std::string& to_zone_;
  };

  /**
   * Charge a response timing to the various dynamic stat postfixes.
   */
  static void chargeResponseTiming(const ResponseTimingInfo& info);

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
