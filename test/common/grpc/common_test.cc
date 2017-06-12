#include "common/grpc/common.h"
#include "common/http/headers.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcCommonTest, getGrpcStatus) {
  Http::TestHeaderMapImpl ok_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Status::Ok, Common::getGrpcStatus(ok_trailers).value());

  Http::TestHeaderMapImpl no_status_trailers{{"foo", "bar"}};
  EXPECT_FALSE(Common::getGrpcStatus(no_status_trailers).valid());

  Http::TestHeaderMapImpl aborted_trailers{{"grpc-status", "10"}};
  EXPECT_EQ(Status::Aborted, Common::getGrpcStatus(aborted_trailers).value());

  Http::TestHeaderMapImpl data_loss_trailers{{"grpc-status", "15"}};
  EXPECT_EQ(Status::DataLoss, Common::getGrpcStatus(data_loss_trailers).value());

  Http::TestHeaderMapImpl invalid_trailers{{"grpc-status", "-1"}};
  EXPECT_EQ(Status::InvalidCode, Common::getGrpcStatus(invalid_trailers).value());
}

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
} // Envoy
