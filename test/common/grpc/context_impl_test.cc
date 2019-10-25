#include <arpa/inet.h>

#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcContextTest, ChargeStats) {
  NiceMock<Upstream::MockClusterInfo> cluster;
  Stats::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool(*symbol_table_);
  const Stats::StatName service = pool.add("service");
  const Stats::StatName method = pool.add("method");
  Context::RequestNames request_names{service, method};
  ContextImpl context(*symbol_table_);
  context.chargeStat(cluster, request_names, true);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(0U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.total").value());

  context.chargeStat(cluster, request_names, false);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.total").value());

  context.chargeRequestMessageStat(cluster, request_names, 3);
  context.chargeResponseMessageStat(cluster, request_names, 4);
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.service.method.request_message_count").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.service.method.response_message_count").value());

  Http::TestHeaderMapImpl trailers;
  Http::HeaderEntry& status = trailers.insertGrpcStatus();
  status.value("0", 1);
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.service.method.total").value());

  status.value("1", 1);
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, &status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.1").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.service.method.total").value());
}

TEST(GrpcContextTest, ResolveServiceAndMethod) {
  std::string service;
  std::string method;
  Http::HeaderMapImpl headers;
  Http::HeaderEntry& path = headers.insertPath();
  path.value(std::string("/service_name/method_name"));
  Stats::TestSymbolTable symbol_table;
  ContextImpl context(*symbol_table);
  absl::optional<Context::RequestNames> request_names = context.resolveServiceAndMethod(&path);
  EXPECT_TRUE(request_names);
  EXPECT_EQ("service_name", symbol_table->toString(request_names->service_));
  EXPECT_EQ("method_name", symbol_table->toString(request_names->method_));
  path.value(std::string(""));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("//"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/service_name"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
  path.value(std::string("/service_name/"));
  EXPECT_FALSE(context.resolveServiceAndMethod(&path));
}

} // namespace Grpc
} // namespace Envoy
