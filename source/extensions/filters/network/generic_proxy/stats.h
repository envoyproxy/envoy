#pragma once

#include <string>

#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * All code and flags that generic proxy filter can emit. This should be global singleton.
 */
class CodeOrFlags : public Singleton::Instance {
public:
  CodeOrFlags(Server::Configuration::ServerFactoryContext& context);

  Stats::StatName statNameFromCode(uint32_t code) const;
  Stats::StatName statNameFromFlag(StreamInfo::ResponseFlag flag) const;

private:
  Stats::StatNamePool pool_;

  std::vector<Stats::StatName> code_stat_names_;
  // The flag_stat_names_ contains stat names of all response flags. The index of each flag
  // is the same as the value of the flag. Size of this vector is the same as the size of
  // StreamInfo::ResponseFlagUtils::responseFlagsVec().
  std::vector<Stats::StatName> flag_stat_names_;

  Stats::StatName unknown_code_or_flag_;
};

/**
 * All generic filter stats. @see stats_macros.h. In addition to following stats, the generic
 * filter also exports stats for every status code and stream info response flag.
 */
#define ALL_GENERIC_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(downstream_rq_total)                                                                     \
  COUNTER(downstream_rq_error)                                                                     \
  COUNTER(downstream_rq_reset)                                                                     \
  COUNTER(downstream_rq_local)                                                                     \
  COUNTER(downstream_rq_decoding_error)                                                            \
  GAUGE(downstream_rq_active, Accumulate)                                                          \
  HISTOGRAM(downstream_rq_time, Milliseconds)                                                      \
  HISTOGRAM(downstream_rq_tx_time, Microseconds)

/**
 * Struct definition for all generic proxy stats. @see stats_macros.h
 */
struct GenericFilterStats {
  ALL_GENERIC_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)

  Stats::StatNameManagedStorage stats_prefix_storage_;
  Stats::StatNameManagedStorage downstream_rq_code_storage_;
  Stats::StatNameManagedStorage downstream_rq_flag_storage_;

  Stats::StatName stats_prefix_{stats_prefix_storage_.statName()};
  Stats::StatName downstream_rq_code_{downstream_rq_code_storage_.statName()};
  Stats::StatName downstream_rq_flag_{downstream_rq_flag_storage_.statName()};

  static GenericFilterStats generateStats(const std::string& prefix, Stats::Scope& stats_scope) {
    return GenericFilterStats{
        ALL_GENERIC_FILTER_STATS(POOL_COUNTER_PREFIX(stats_scope, prefix),
                                 POOL_GAUGE_PREFIX(stats_scope, prefix),
                                 POOL_HISTOGRAM_PREFIX(stats_scope, prefix))
            Stats::StatNameManagedStorage{prefix, stats_scope.symbolTable()},
        Stats::StatNameManagedStorage{"downstream_rq_code", stats_scope.symbolTable()},
        Stats::StatNameManagedStorage{"downstream_rq_flag", stats_scope.symbolTable()},
    };
  }
};

class GenericFilterStatsHelper {
public:
  GenericFilterStatsHelper(const CodeOrFlags& code_or_flag, GenericFilterStats& stats,
                           Stats::Scope& stats_scope)
      : code_or_flag_(code_or_flag), stats_(stats), stats_scope_(stats_scope) {}

  void onRequestReset();
  void onRequestDecodingError();
  void onRequest();
  void onRequestComplete(const StreamInfo::StreamInfo& info, bool local_reply, bool error_reply);

private:
  const CodeOrFlags& code_or_flag_;
  GenericFilterStats& stats_;
  Stats::Scope& stats_scope_;

  // The following two pairs are used to avoid stats name creation and symbol table lookup
  // for every request. In all practical cases, there most requests will have the same
  // status code and response flag.
  // When a request is completed, we will check if the status code or response flag is
  // same as the last one. If so, we will use the same counter. Otherwise, we will try to
  // find the counter in the scope and update these pairs.
  // Even in the worst case where the status code and response flag are different for every
  // request, we will only do some additional integer comparisons and local variable
  // assignments which should be very cheap.
  std::pair<uint32_t, OptRef<Stats::Counter>> last_code_counter_{};
  std::pair<StreamInfo::ResponseFlag, OptRef<Stats::Counter>> last_flag_counter_{};
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
