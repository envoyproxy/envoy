#include "envoy/common/platform.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/global.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GrpcContextTest, ChargeStats) {
  NiceMock<Upstream::MockClusterInfo> cluster;
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool(*symbol_table_);
  const Stats::StatName service = pool.add("service");
  const Stats::StatName method = pool.add("method");
  Context::RequestStatNames request_names{service, method};
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

  context.chargeRequestMessageStat(cluster, {}, 3);
  context.chargeResponseMessageStat(cluster, {}, 4);
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.request_message_count").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.response_message_count").value());

  Http::TestResponseTrailerMapImpl trailers;
  trailers.setGrpcStatus("0");
  const Http::HeaderEntry* status = trailers.GrpcStatus();
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(3U, cluster.stats_store_.counter("grpc.service.method.total").value());

  trailers.setGrpcStatus("1");
  context.chargeStat(cluster, Context::Protocol::Grpc, request_names, status);
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.0").value());
  EXPECT_EQ(1U, cluster.stats_store_.counter("grpc.service.method.1").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.success").value());
  EXPECT_EQ(2U, cluster.stats_store_.counter("grpc.service.method.failure").value());
  EXPECT_EQ(4U, cluster.stats_store_.counter("grpc.service.method.total").value());
}

TEST(GrpcContextTest, ResolveServiceAndMethod) {
  std::string service;
  std::string method;
  Http::TestRequestHeaderMapImpl headers;
  headers.setPath("/service_name/method_name?a=b");
  const Http::HeaderEntry* path = headers.Path();
  Stats::TestUtil::TestSymbolTable symbol_table;
  ContextImpl context(*symbol_table);
  absl::optional<Context::RequestStatNames> request_names =
      context.resolveDynamicServiceAndMethod(path);
  EXPECT_TRUE(request_names);
  EXPECT_EQ("service_name", absl::get<Stats::DynamicSavedName>(request_names->service_));
  EXPECT_EQ("method_name", absl::get<Stats::DynamicSavedName>(request_names->method_));
  headers.setPath("");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethod(path));
  headers.setPath("/");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethod(path));
  headers.setPath("//");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethod(path));
  headers.setPath("/service_name");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethod(path));
  headers.setPath("/service_name/");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethod(path));
}

TEST(GrpcContextTest, ResolvedServiceAndMethodOutliveChangesInRequestNames) {
  Http::TestRequestHeaderMapImpl headers;
  headers.setPath("/service_name/method_name?a=b");
  const Http::HeaderEntry* path = headers.Path();
  Stats::TestUtil::TestSymbolTable symbol_table;
  ContextImpl context(*symbol_table);

  auto request_names = context.resolveDynamicServiceAndMethod(path);

  EXPECT_TRUE(request_names);
  EXPECT_EQ("service_name", absl::get<Stats::DynamicSavedName>(request_names->service_));
  EXPECT_EQ("method_name", absl::get<Stats::DynamicSavedName>(request_names->method_));

  headers.setPath("/service_name1/method1");
  // old values stay the same
  EXPECT_EQ("service_name", absl::get<Stats::DynamicSavedName>(request_names->service_));
  EXPECT_EQ("method_name", absl::get<Stats::DynamicSavedName>(request_names->method_));

  auto new_request_names = context.resolveDynamicServiceAndMethod(path);
  EXPECT_EQ("service_name1", absl::get<Stats::DynamicSavedName>(new_request_names->service_));
  EXPECT_EQ("method1", absl::get<Stats::DynamicSavedName>(new_request_names->method_));
}

TEST(GrpcContextTest, resolveDynamicServiceAndMethodWithDotReplaced) {
  Http::TestRequestHeaderMapImpl headers;
  headers.setPath("/foo.bar.zoo/method.name?a=b");
  const Http::HeaderEntry* path = headers.Path();
  Stats::TestUtil::TestSymbolTable symbol_table;
  ContextImpl context(*symbol_table);
  absl::optional<Context::RequestStatNames> request_names;
  request_names = context.resolveDynamicServiceAndMethodWithDotReplaced(path);
  EXPECT_TRUE(request_names);
  EXPECT_EQ("foo_bar_zoo", absl::get<Stats::DynamicSavedName>(request_names->service_));
  EXPECT_EQ("method.name", absl::get<Stats::DynamicSavedName>(request_names->method_));
  headers.setPath("");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethodWithDotReplaced(path));
  headers.setPath("/");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethodWithDotReplaced(path));
  headers.setPath("//");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethodWithDotReplaced(path));
  headers.setPath("/service_name");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethodWithDotReplaced(path));
  headers.setPath("/service_name/");
  EXPECT_FALSE(context.resolveDynamicServiceAndMethodWithDotReplaced(path));
}

} // namespace Grpc
} // namespace Envoy
