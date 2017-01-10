#include "common/grpc/common.h"
#include "common/http/headers.h"

#include "test/generated/helloworld.pb.h"
#include "test/mocks/upstream/mocks.h"

namespace Grpc {

TEST(GrpcCommonTest, chargeStats) {
  NiceMock<Upstream::MockClusterInfo> cluster;
  Common::chargeStat(cluster, "service", "method", true);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(0U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.total").value());

  Common::chargeStat(cluster, "service", "method", false);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.total").value());
}

TEST(GrpcCommonTest, prepareHeaders) {
  Http::MessagePtr message = Common::prepareHeaders("cluster", "service_name", "method_name");

  EXPECT_STREQ("POST", message->headers().Method()->value().c_str());
  EXPECT_STREQ("/service_name/method_name", message->headers().Path()->value().c_str());
  EXPECT_STREQ("cluster", message->headers().Host()->value().c_str());
  EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());
}

} // Grpc
