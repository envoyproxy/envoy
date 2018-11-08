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
  explicit CodeStatsImpl(Stats::SymbolTable& symbol_table);
  ~CodeStatsImpl() override;

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                               Code response_code) override;
  void chargeResponseStat(const ResponseStatInfo& info) override;
  void chargeResponseTiming(const ResponseTimingInfo& info) override;

private:
  static absl::string_view stripTrailingDot(absl::string_view prefix);
  static std::string join(const std::vector<absl::string_view>& v);
  Stats::StatName makeStatName(absl::string_view name);
  Stats::StatName upstreamRqGroup(Code response_code) const;

  // We have to actively free the StatNameStorage with the symbol_table_, so
  // it's easiest to accumulate the StatNameStorage objects in a vector, in
  // addition to having discrete member variables. That saves having to
  // enumerate the stat-names in both the member-variables listed below
  // and the destructor.
  //
  // TODO(jmarantz): consider a new variant in stats_macros.h to enumerate stats
  // names and manage their storage.
  std::vector<Stats::StatNameStorage> storage_;

  Stats::SymbolTable& symbol_table_;
  Stats::StatName canary_upstream_rq_completed_;
  Stats::StatName canary_upstream_rq_time_;
  Stats::StatName external_rq_time_;
  Stats::StatName external_upstream_rq_completed_;
  Stats::StatName external_upstream_rq_time_;
  Stats::StatName internal_rq_time_;
  Stats::StatName internal_upstream_rq_completed_;
  Stats::StatName internal_upstream_rq_time_;
  Stats::StatName upstream_rq_completed_;
  Stats::StatName upstream_rq_time_;
  Stats::StatName upstream_rq_time;
  Stats::StatName vcluster_;
  Stats::StatName vhost_;
  Stats::StatName zone_;
  Stats::StatName response_code_2xx_;
  Stats::StatName response_code_3xx_;
  Stats::StatName response_code_4xx_;
  Stats::StatName response_code_5xx_;

  // We keep several stats for each HTTP response codes, created lazily -- as
  // most response-codes are never seen. We do a poor man's locking here by
  // keeping them in an array. To minimize contention generally, we'll just have
  // a r/w lock per response-code.
  using LockedStatName = std::pair<absl::Mutex, std::unique_ptr<Stats::StatNameStorage>>;
  constexpr MaxResponseCode = 600;
  LockedStat[MaxResponseCode] canary_upstream_rq_;
  LockedStat[MaxResponseCode] external_upstream_rq_;
  LockedStat[MaxResponseCode] internal_upstream_rq_;
  LockedStat[MaxResponseCode] upstream_rq_;
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
