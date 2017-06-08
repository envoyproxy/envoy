#include "common/grpc/common.h"
#include "common/http/headers.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"

#include "gtest/gtest.h"

namespace Envoy {
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

TEST(GrpcCommonTest, resolveServiceAndMethod) {
  std::string service;
  std::string method;
  Http::HeaderMapImpl headers;
  Http::HeaderEntry& path = headers.insertPath();
  path.value(std::string("/service_name/method_name"));
  EXPECT_TRUE(Common::resolveServiceAndMethod(&path, &service, &method));
  EXPECT_EQ("service_name", service);
  EXPECT_EQ("method_name", method);
  path.value(std::string(""));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("//"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/service_name"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
  path.value(std::string("/service_name/"));
  EXPECT_FALSE(Common::resolveServiceAndMethod(&path, &service, &method));
}

} // Grpc
} // Envoy
