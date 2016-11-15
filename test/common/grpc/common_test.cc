#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/stats/stats_impl.h"

#include "test/generated/helloworld.pb.h"
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

TEST(CommonTest, prepareHeaders) {
  Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name");

  EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
  EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
  EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
  EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
}

} // Grpc
