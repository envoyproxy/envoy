#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Http {

class CodeStatsImpl : public CodeStats {
public:
  explicit CodeStatsImpl(Stats::SymbolTable& symbol_table);
  ~CodeStatsImpl() override;

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                               Code response_code) override;
  void chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                               Code response_code) override;
  void chargeResponseStat(const ResponseStatInfo& info) override;
  void chargeResponseTiming(const ResponseTimingInfo& info) override;

private:
  using Join = Stats::StatNameJoiner;

  class RequestCodeGroup {
  public:
    RequestCodeGroup(absl::string_view prefix, CodeStatsImpl& code_stats)
        : code_stats_(code_stats), prefix_(std::string(prefix)),
          upstream_rq_200_(makeStatName(Code::OK)),
          upstream_rq_404_(makeStatName(Code::NotFound)),
          upstream_rq_503_(makeStatName(Code::ServiceUnavailable)) {}
    ~RequestCodeGroup();

    Stats::StatName statName(Code response_code);

    /*
    using LockedStatName = std::pair<absl::Mutex, std::unique_ptr<Stats::StatNameStorage>>;

    // We'll cover known HTTP status codes in a mapped array, which we'll
    // discover by calling CodeUtility::toString(). Of course the response-code
    // can be any 64-bit integer as far as we can tell from this class, so
    // we'll have a fallback flast hash map for those.

    constexpr MaxResponseCode = 600;
    LockStatName[MaxResponseCode] locked_stat_names_;
    */

  private:
    using RCStatNameMap = absl::flat_hash_map<Code, std::unique_ptr<Stats::StatNameStorage>>;

    Stats::StatName makeStatName(Code response_code);

    CodeStatsImpl& code_stats_;
    std::string prefix_;
    absl::Mutex mutex_;
    RCStatNameMap rc_stat_name_map_ GUARDED_BY(mutex_);
    Stats::StatName upstream_rq_200_;
    Stats::StatName upstream_rq_404_;
    Stats::StatName upstream_rq_503_;
  };

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

  Stats::StatName canary_;
  Stats::StatName canary_upstream_rq_time_;
  Stats::StatName external_;
  Stats::StatName external_rq_time_;
  Stats::StatName external_upstream_rq_time_;
  Stats::StatName internal_;
  Stats::StatName internal_rq_time_;
  Stats::StatName internal_upstream_rq_time_;
  Stats::StatName upstream_;
  Stats::StatName upstream_rq_1xx_;
  Stats::StatName upstream_rq_2xx_;
  Stats::StatName upstream_rq_3xx_;
  Stats::StatName upstream_rq_4xx_;
  Stats::StatName upstream_rq_5xx_;
  Stats::StatName upstream_rq_unknown_;
  Stats::StatName upstream_rq_completed_;
  Stats::StatName upstream_rq_time;
  Stats::StatName upstream_rq_time_;
  Stats::StatName vcluster_;
  Stats::StatName vhost_;
  Stats::StatName zone_;

  RequestCodeGroup canary_upstream_rq_;
  RequestCodeGroup external_upstream_rq_;
  RequestCodeGroup internal_upstream_rq_;
  RequestCodeGroup upstream_rq_;
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
