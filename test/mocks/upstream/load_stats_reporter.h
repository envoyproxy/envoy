#pragma once

#include "envoy/stats/scope.h"
#include "envoy/upstream/load_stats_reporter.h"

#include "source/common/stats/isolated_store_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class MockLoadStatsReporter : public LoadStatsReporter {
public:
  MockLoadStatsReporter();
  ~MockLoadStatsReporter() override;

  MOCK_METHOD(const LoadReporterStats&, getStats, (), (const, override));

  Stats::IsolatedStoreImpl store_;
  LoadReporterStats stats_;
};

} // namespace Upstream
} // namespace Envoy
