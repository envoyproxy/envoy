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

  EXPECT_EQ("http", message->headers().get(Http::Headers::get().Scheme));
  EXPECT_EQ("POST", message->headers().get(Http::Headers::get().Method));
  EXPECT_EQ("/service_name/method_name", message->headers().get(Http::Headers::get().Path));
  EXPECT_EQ("cluster", message->headers().get(Http::Headers::get().Host));
  EXPECT_EQ("application/grpc", message->headers().get(Http::Headers::get().ContentType));
}

} // Grpc
