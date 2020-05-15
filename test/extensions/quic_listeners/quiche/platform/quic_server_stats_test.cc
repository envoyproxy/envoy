#include "common/memory/stats.h"
#include "common/stats/stats_matcher_impl.h"
#include "common/stats/symbol_table_impl.h"
#include "common/stats/tag_producer_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/thread_local/thread_local_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_server_stats.h"

namespace quic {

class QuicServerStatsTest : public ::testing::Test {
public:
  QuicServerStatsTest()
      : symbol_table_(SymbolTableCreator::makeSymbolTable()), alloc_(*symbol_table_),
        store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)) {
    store_->addSink(sink_);
  }

protected:
  Envoy::Stats::SymbolTablePtr symbol_table_;
  NiceMock<Envoy::Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<Envoy::ThreadLocal::MockInstance> tls_;
  Envoy::Stats::AllocatorImpl alloc_;
  Envoy::Stats::MockSink sink_;
  std::unique_ptr<Envoy::Stats::ThreadLocalStoreImpl> store_;
};

TEST_F(QuicServerStatsTest, HistogramCounts) {
  Histogram& h1 = store_->histogram("quic_server_num_written_packets_per_write",
                                    Stats::Histogram::Unit::Unspecified);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h1), 100));
  QUIC_SERVER_HISTOGRAM_COUNTS(quic_server_num_written_packets_per_write, 16, 0, 100, 10, "aaaa");
  Histogram& h2 = store_->histogram("test_trial.success", Stats::Histogram::Unit::Unspecified);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h2), 100));
  QUIC_SERVER_HISTOGRAM_BOOL(test_trial.success, "just for test");
  Histogram& h3 = store_->histogram("test_latency", Stats::Histogram::Unit::Milliseconds);
  EXPECT_CALL(sink_, onHistogramComplete(Ref(h3), 100));
  QUIC_SERVER_HISTOGRAM_TIMES(test_latency, 100, 0, 500, 10, "bbb");
}

} // namespace quic

#define RETURN_PREFIXED_STRING_LITERAL(dummy, x)                                                   \
  case x:                                                                                          \
    return PREFIX #x;
#define TMP_CLASS_HELPER(A, X)
class Tmp##A {
  X
};
Tmp##A A;

#define TMP_CLASS(Type, A) #Type A;

#define TMP_CLASS(Type, A, B) TMP_CLASS_HELPER(A, TMP_CLASS(Type, B))

#define TMP_CLASS(Type, A, B, C) TMP_CLASS_HELPER(A, TMP_CLASS(Type, B, C))

#define TMP_CLASS(Type, A, B, C, D) TMP_CLASS_HELPER(A, TMP_CLASS(Type, B, C, D))

#define QUICHE_HISTOGRAMS(DECLARE)                                                                 \
  DECLARE(Unspecified, quic_server_num_written_packets_per_write)                                  \
  DECLARE(Unspecified, quiche_test_trial, success)                                                 \
  DECLARE(Milliseconds, quiche_test_latency)

#define QUICHE_COUNTERS(DECLARE) DECLARE("", quiche_test_counter)

#define QUICHE_ENUM_HISTOGRAMS(DECLARE) DECLARE("", quic_server_connection_close_errors)

#define QUICHE_GAUGES(DECLARE) DECLARE(NeverImport, quiche_test_gauge)

using IntToMetricMap = absl::flat_hash_map<uint32_t, std::reference_wrapper<Envoy::Stats::Metric>>;

#define GENERATE_STATS_STRUCT_MEMBER(dummy, name, name_suffix...)                                  \
  TMP_CLASS(Envoy::Stats::Metric&, name, name_suffix)
#define GENERATE_STATS_STRUCT_MAP_MEMBER(dummy, name, name_suffix...)                              \
  TMP_CLASS(IntToMetricMap, name, name_suffix)

/**
 * struct QuicheStats {
 *   // QUIC_SERVER_HISTOGRAM_BOOL(a.b.c.d, ...)
 *   class Tmp_a {
 *     class Tmp_b {
 *       class Tmp_c {
 *         Envoy::Stats::Metric& d;
 *       };
 *       Tmp_c c;
 *     };
 *     Tmp_b b;
 *   };
 *   Tmp_a a;
 *   // QUIC_SERVER_HISTOGRAM_COUNTER(aaa, ...)
 *   Envoy::Stats::Metric& aaa;
 *   // QUIC_SERVER_HISTOGRAM_ENUM(bbb, ...)
 *   IntToMetricMap bbb;
 *   ...
 * };
 **/
struct QuicheStats {
  QUICHE_HISTOGRAMS(GENERATE_STATS_STRUCT_MEMBER)
  QUICHE_COUNTERS(GENERATE_STATS_STRUCT_MEMBER)
  QUICHE_GAUGES(GENERATE_STATS_STRUCT_MEMBER)
  QUICHE_ENUM_HISTOGRAMS(GENERATE_STATS_STRUCT_MAP_MEMBER)
};

#define TMP_CLASS_HELPER(A, X)                                                                     \
  struct A_t {                                                                                     \
    X                                                                                              \
  };                                                                                               \
  A_t A;

#define TMP_CLASS1(Type, A) Type A;

#define TMP_CLASS2(Type, A, B) TMP_CLASS_HELPER(A, TMP_CLASS1(Type, B))

#define TMP_CLASS3(Type, A, B, C) TMP_CLASS_HELPER(A, TMP_CLASS2(Type, B, C))

#define TMP_CLASS4(Type, A, B, C, D) TMP_CLASS_HELPER(A, TMP_CLASS3(Type, B, C, D))

DECLARE(success, HISTOGRAM, HISTOGRAM_UNIT_UNSPECIFIED, "test_trial.success"),
    DECLARE(failure, STRUCT_1, DECLARE(reason, ENUM_HISTOGRAM, "test_trial.failure.reason"))
        __VA_OPT__(, ) __VA_ARGS__
