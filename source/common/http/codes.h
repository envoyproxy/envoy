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

  void writeCategory(const ResponseStatInfo& info, Stats::StatName rq_group,
                     Stats::StatName rq_code, Stats::StatName category) const;
  void incCounter(Stats::Scope& scope, const Stats::StatNameVec& names) const;
  void incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const;
  void recordHistogram(Stats::Scope& scope, const Stats::StatNameVec& names,
                       Stats::Histogram::Unit unit, uint64_t count) const;

  Stats::StatName upstreamRqGroup(Code response_code) const;
  Stats::StatName upstreamRqStatName(Code response_code) const;

  mutable Stats::StatNamePool stat_name_pool_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  Stats::SymbolTable& symbol_table_;

  const Stats::StatName canary_;
  const Stats::StatName empty_; // Used for the group-name for invalid http codes.
  const Stats::StatName external_;
  const Stats::StatName internal_;
  const Stats::StatName upstream_;
  const Stats::StatName upstream_rq_1xx_;
  const Stats::StatName upstream_rq_2xx_;
  const Stats::StatName upstream_rq_3xx_;
  const Stats::StatName upstream_rq_4xx_;
  const Stats::StatName upstream_rq_5xx_;
  const Stats::StatName upstream_rq_unknown_;
  const Stats::StatName upstream_rq_completed_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName vcluster_;
  const Stats::StatName vhost_;
  const Stats::StatName zone_;

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
  //
  // There can be multiple symbol tables in a server. The one passed into the
  // Codes constructor should be the same as the one passed to
  // Stats::ThreadLocalStore. Note that additional symbol tables can be created
  // from IsolatedStoreImpl's default constructor.
  //
  // The Codes object is global to the server.

  static constexpr uint32_t NumHttpCodes = 500;
  static constexpr uint32_t HttpCodeOffset = 100; // code 100 is at index 0.
  mutable std::atomic<const uint8_t*> rc_stat_names_[NumHttpCodes];
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
