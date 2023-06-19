#include "test/server/admin/stats_render_test_base.h"

namespace Envoy {
namespace Server {

StatsRenderTestBase::StatsRenderTestBase() : alloc_(symbol_table_), store_(alloc_) {
  store_.addSink(sink_);
  store_.initializeThreading(main_thread_dispatcher_, tls_);
}

StatsRenderTestBase::~StatsRenderTestBase() {
  tls_.shutdownGlobalThreading();
  store_.shutdownThreading();
  tls_.shutdownThread();
}

Stats::ParentHistogram& StatsRenderTestBase::populateHistogram(const std::string& name,
                                                               const std::vector<uint64_t>& vals) {
  Stats::Histogram& h = store_.histogramFromString(name, Stats::Histogram::Unit::Unspecified);
  for (uint64_t val : vals) {
    h.recordValue(val);
  }
  store_.mergeHistograms([]() -> void {});
  return dynamic_cast<Stats::ParentHistogram&>(h);
}

} // namespace Server
} // namespace Envoy
