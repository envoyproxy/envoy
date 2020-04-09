#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#define HISTOGRAM_UNIT_UNSPECIFIED Envoy::Stats::Histogram::Unit::Unspecified
#define HISTOGRAM_UNIT_MILLISECONDS Envoy::Stats::Histogram::Unit::Milliseconds
#define GAUGE_MODE_NEVER_IMPORT Envoy::Stats::Gauge::ImportMode::NeverImport

// A full list of QUICHE stats names which is synchoronized with QUICHE.
// New stats needs to be added to the list otherwise the build will complain
// about undefined variable.
// Deprecated stats nees to be removed from the list otherwise clang will
// trigger the Wunused-member-function warning.
// If a QUICHE stats name doesn't contain '.', it can be declared as one of
// HISTOGRAM, GAUGE, COUNTER and ENUM_HISTOGRAM. The format for such type is
// TYPE(name_in_quiche, [other_param_for_envoy_stats,] "name_in_envoy")
// If a QIUCHE stats name contains '.', it need to be declared as nested STRUCT, one layer for
// each token before '.', and the last token as one of HISTOGRAM, GAUGE, COUNTER and ENUM_HISTOGRAM.
// The format for stats name "prefix1.prefix2.prefix3.token" looks like STRUCT(prefix1,
// STRUCT(prefix2, STRUCT(prefix3, TYPE(token, [other_param_for_envoy_stats,] "name_in_envoy"))))
// Two QUICHE stats names can share common prefix in which case the different
// parts after the common prefix are wrapped in the same STRUCT, i.e.
// For histogram names "prefix1.token1" and "prefix1.token2" are like
// STRUCT(prefix1, HISTOGRAM(token1, HISTOGRAM_UNIT_UNSPECIFIED, "unique_name1")
//                 HISTOGRAM(token2, HISTOGRAM_UNIT_MILLISECONDS, "unique_name2"))
#define QUICHE_STATS(COUNTER, GAUGE, HISTOGRAM, ENUM_HISTOGRAM, STRUCT)                            \
  HISTOGRAM(test_latency, HISTOGRAM_UNIT_MILLISECONDS, "test_latency")                             \
  STRUCT(test_trial, HISTOGRAM(success, HISTOGRAM_UNIT_UNSPECIFIED, "test_trial.success")          \
                         STRUCT(failure, ENUM_HISTOGRAM(reason, "test_trial.failure.reason"))      \
                             HISTOGRAM(cancel, HISTOGRAM_UNIT_UNSPECIFIED, "test_trial.cancel"))   \
  STRUCT(test_session, STRUCT(a, STRUCT(b, STRUCT(c, HISTOGRAM(d, HISTOGRAM_UNIT_UNSPECIFIED,      \
                                                               "test_session.a.b.c.d")))))         \
  /*  To be added when QUICHE interfaceis updated                                                  \
  COUNTER(test_counter , "quiche_test_counter")                                                    \
  GAUGE(test_gauge , GAUGE_MODE_NEVER_IMPORT, "quiche_test_gauge")                                 \
  HISTOGRAM(quic_server_num_written_packets_per_write, HISTOGRAM_UNIT_UNSPECIFIED,                 \
          "quic_server_num_written_packets_per_write")                                             \
  ENUM_HISTOGRAM(quic_server_connection_close_errors, "quic_server_connection_close_errors")       \
  ENUM_HISTOGRAM(quic_client_connection_close_errors, "quic_client_connection_close_errors")       \
  */

using CounterMap = absl::flat_hash_map<uint32_t, std::reference_wrapper<Envoy::Stats::Counter>>;

#define STRUCT_MEMBER_HISTOGRAM(a, other_param, name_str)                                          \
  Envoy::Stats::Histogram& a;                                                                      \
  Envoy::Stats::Histogram& a##_() { return a; }
#define STRUCT_MEMBER_COUNTER(a, name_str)                                                         \
  Envoy::Stats::Counter& a;                                                                        \
  Envoy::Stats::Counter& a##_() { return a; }
#define STRUCT_MEMBER_GAUGE(a, other_param, name_str)                                              \
  Envoy::Stats::Gauge& a;                                                                          \
  Envoy::Stats::Gauge& a##_() { return a; }
// Used to support QUIC_SERVER_HISTOGRAM_ENUM. Envoy histogram is not designed
// for collecting enum values. Because the bucketization is hidden from the interface.
// A walk around is to use a group of couters mapped by enum value. Each value
// mapped counter is not instantiated till it is used.
#define STRUCT_MEMBER_ENUM_HISTOGRAM(a, name_str)                                                  \
  CounterMap a;                                                                                    \
  CounterMap& a##_() { return a; }
// Used to support '.' concatinated stats name, i.e. QUIC_SERVER_HISTOGRAM_BOOL(a.b.c.d, false, ...)
#define STRUCT_MEMBER_STRUCT(a, X)                                                                 \
  struct a##_t {                                                                                   \
    X                                                                                              \
  } a;

#define EMPTY_MAP(args...)                                                                         \
  {}

#define INIT_STATS_HISTOGRAM(a, other_param, name_str)                                             \
  scope.histogramFromString("quiche." name_str, other_param),
#define INIT_STATS_COUNTER(a, name_str) scope.counterFromString("quiche." name_str),
#define INIT_STATS_GAUGE(a, other_param, name_str)                                                 \
  scope.gaugeFromString("quiche." name_str, other_param),
#define INIT_STATS_ENUM_HISTOGRAM(a, name_str) EMPTY_MAP(a, name_str),
#define INIT_STATS_STRUCT(name, X...) {X},

namespace {

// In order to detect deprecated stats, these entries need to be declared in a struct as public
// fields with corresponding getters. And they should be only accessed via their getters as if they
// are private field. The reason for them to be public is for the accessibility of stats like
// "a.b.c.d". When a stats is removed from QUICHE code, its getter is left unused. And this struct
// must be declared in an annonymous namespace. This struct looks like:
//  struct QuicheStats {
//    // QUIC_SERVER_HISTOGRAM_BOOL(a.b.c.d, ...)
//    // QUIC_SERVER_HISTOGRAM_ENUM(a.b.c.e, ...)
//    struct a_t{
//      struct b_t{
//        struct c_t{
//          Envoy::Stats::Metric& d;
//          Envoy::Stats::Metric& d_() { return d; }
//          EnumMap e;
//          EnumMap& e_() { return e; }
//        } c;
//      } b;
//    } a;
//
//    // QUIC_SERVER_HISTOGRAM_COUNTER(aaa, ...)
//    Envoy::Stats::Histogram& aaa;
//    Envoy::Stats::Histogram& aaa_() { return aaa; }
//
//    // QUIC_SERVER_HISTOGRAM_ENUM(bbb, ...)
//    // Counters are not instantiated until incremented.
//    EnumMap bbb;
//    EnumMap& bbb_() { return bbb; }
//    ...
//  };
struct QuicheStats {
  QUICHE_STATS(STRUCT_MEMBER_COUNTER, STRUCT_MEMBER_GAUGE, STRUCT_MEMBER_HISTOGRAM,
               STRUCT_MEMBER_ENUM_HISTOGRAM, STRUCT_MEMBER_STRUCT)
};

} // namespace

class QuicStatsImpl {
public:
  void initializeStats(Envoy::Stats::Scope& scope) {
    scope_ = &scope;
    std::string prefix("quiche");
    stats_.reset(
        new QuicheStats{// // QUIC_SERVER_HISTOGRAM_COUNTER(aaa, ...)
                        // scope.histogram("quiche.aaa", ...),
                        //
                        // // QUIC_SERVER_HISTOGRAM_ENUM(bbb, ...)
                        // {},
                        //
                        // // QUIC_SERVER_HISTOGRAM_ENUM(a.b.c.d, ...)
                        // // QUIC_SERVER_HISTOGRAM_ENUM(a.b.c.e, ...)
                        // {{{ {},{} }}},
                        QUICHE_STATS(INIT_STATS_COUNTER, INIT_STATS_GAUGE, INIT_STATS_HISTOGRAM,
                                     INIT_STATS_ENUM_HISTOGRAM, INIT_STATS_STRUCT)});
  }

  void resetForTest() {
    stats_.reset();
    scope_ = nullptr;
  }

  Envoy::Stats::Counter& createCounter(std::string name) { return scope_->counterFromString(name); }

  QuicheStats& stats() {
    RELEASE_ASSERT(stats_ != nullptr, "Quiche stats is not initialized_.");
    return *stats_;
  }

private:
  Envoy::Stats::Scope* scope_;
  std::unique_ptr<QuicheStats> stats_;
};

QuicStatsImpl& getQuicheStats() {
  // Each worker thread has its own set of references to counters, gauges and
  // histograms.
  static thread_local QuicStatsImpl stats_impl = QuicStatsImpl();
  return stats_impl;
}

#define QUIC_SERVER_HISTOGRAM_ENUM_IMPL(enum_name, sample, enum_size, docstring)                   \
  do {                                                                                             \
    CounterMap& enum_map = getQuicheStats().stats().enum_name##_();                                \
    uint32_t key = static_cast<uint32_t>(sample);                                                  \
    auto it = enum_map.find(key);                                                                  \
    if (it == enum_map.end()) {                                                                    \
      auto result = enum_map.emplace(                                                              \
          key, getQuicheStats().createCounter(absl::StrCat("quiche." #enum_name ".", sample)));    \
      ASSERT(result.second);                                                                       \
      it = result.first;                                                                           \
    }                                                                                              \
    it->second.get().inc();                                                                        \
  } while (0)

#define QUIC_SERVER_HISTOGRAM_BOOL_IMPL(name, sample, docstring)                                   \
  QUIC_SERVER_HISTOGRAM_TIMES_IMPL(name, sample, 0, 1, 2, docstring)

#define QUIC_SERVER_HISTOGRAM_TIMES_IMPL(time_name, sample, min, max, bucket_count, docstring)     \
  do {                                                                                             \
    static_cast<Envoy::Stats::Histogram&>(getQuicheStats().stats().time_name##_())                 \
        .recordValue(sample);                                                                      \
  } while (0)

#define QUIC_SERVER_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, bucket_count, docstring)         \
  QUIC_SERVER_HISTOGRAM_TIMES_IMPL(name, sample, min, max, bucket_count, docstring)
