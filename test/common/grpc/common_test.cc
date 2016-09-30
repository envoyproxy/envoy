#include "common/grpc/common.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/stats/mocks.h"

namespace Grpc {

TEST(CommonTest, chargeStats) {
  Stats::IsolatedStoreImpl stats;

  Common::chargeStat(stats, "cluster", "service", "method", true);
  EXPECT_EQ(1U, stats.counter("cluster.cluster.grpc.service.method.success").value());
  EXPECT_EQ(0U, stats.counter("cluster.cluster.grpc.service.method.failure").value());
  EXPECT_EQ(1U, stats.counter("cluster.cluster.grpc.service.method.total").value());

  Common::chargeStat(stats, "cluster", "service", "method", false);
  EXPECT_EQ(1U, stats.counter("cluster.cluster.grpc.service.method.success").value());
  EXPECT_EQ(1U, stats.counter("cluster.cluster.grpc.service.method.failure").value());
  EXPECT_EQ(2U, stats.counter("cluster.cluster.grpc.service.method.total").value());
}

TEST(CommonTest, serializeBody) {}

TEST(CommonTest, prepareHeaders) {}

} // Grpc
