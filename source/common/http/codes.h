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

struct CodeStats::ResponseStatInfo {
  Stats::Scope& global_scope_;
  Stats::Scope& cluster_scope_;
  Stats::StatName prefix_;
  uint64_t response_status_code_;
  bool internal_request_;
  Stats::StatName request_vhost_name_;
  Stats::StatName request_vcluster_name_;
  Stats::StatName from_zone_;
  Stats::StatName to_zone_;
  bool upstream_canary_;
};

struct CodeStats::ResponseTimingInfo {
  Stats::Scope& global_scope_;
  Stats::Scope& cluster_scope_;
  Stats::StatName prefix_;
  std::chrono::milliseconds response_time_;
  bool upstream_canary_;
  bool internal_request_;
  Stats::StatName request_vhost_name_;
  Stats::StatName request_vcluster_name_;
  Stats::StatName from_zone_;
  Stats::StatName to_zone_;
};

class CodeStatsImpl : public CodeStats {
public:
  explicit CodeStatsImpl(Stats::SymbolTable& symbol_table);

  // CodeStats
  void chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                               Code response_code) const override;
  void chargeResponseStat(const ResponseStatInfo& info) const override;
  void chargeResponseTiming(const ResponseTimingInfo& info) const override;

private:
  friend class CodeStatsTest;

  void incCounter(Stats::Scope& scope, const std::vector<Stats::StatName>& names) const;
  void incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const;
  void recordHistogram(Stats::Scope& scope, const std::vector<Stats::StatName>& names,
                       uint64_t count) const;

  class RequestCodeGroup {
  public:
    RequestCodeGroup(absl::string_view prefix, CodeStatsImpl& code_stats);

    Stats::StatName statName(Code response_code);

  private:
    // Use an array of atomic pointers to hold StatNameStorage objects for
    // every conceivable HTTP response code. In the hot-path we'll reference
    // these with a null-check, and if we need to allocate a symbol for a
    // new code, we'll take a mutex to avoid duplicate allocations and
    // subsequent leaks. This is similar in principle to a ReaderMutexLock,
    // but should be faster, as ReaderMutexLocks appear to be too expensive for
    // fine-grained controls. Another option would be to use a lock per
    // stat-name, which might have similar performance to atomics with default
    // barrier policy.
    //
    // We don't allocate these all up front during construction because
    // SymbolTable greedily encodes the first 128 names it discovers in one
    // byte. We don't want those high-value single-byte codes to go to fully
    // enumerating the 4 prefixes combined with HTTP codes that are seldom used,
    // so we allocate these on demand.

    static constexpr uint32_t NumHttpCodes = 1000;
    std::atomic<uint8_t*> rc_stat_names_[NumHttpCodes];

    CodeStatsImpl& code_stats_;
    std::string prefix_;
    absl::Mutex mutex_;
    Stats::StatNamePool stat_name_pool_ GUARDED_BY(mutex_);
  };

  static absl::string_view stripTrailingDot(absl::string_view prefix);
  Stats::StatName makeStatName(absl::string_view name) { return stat_name_pool_.add(name); }
  Stats::StatName upstreamRqGroup(Code response_code) const;

  Stats::StatNamePool stat_name_pool_;
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

  mutable RequestCodeGroup canary_upstream_rq_;
  mutable RequestCodeGroup external_upstream_rq_;
  mutable RequestCodeGroup internal_upstream_rq_;
  mutable RequestCodeGroup upstream_rq_;
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
