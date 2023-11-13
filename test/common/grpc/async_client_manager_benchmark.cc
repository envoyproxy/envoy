#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/async_client_manager_impl.h"

#include "test/benchmark/main.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

class AsyncClientManagerImplTest {
public:
  AsyncClientManagerImplTest()
      : api_(Api::createApiForTest()), stat_names_(scope_.symbolTable()),
        async_client_manager_(
            cm_, tls_, test_time_.timeSystem(), *api_, stat_names_,
            envoy::config::bootstrap::v3::Bootstrap::GrpcAsyncClientManagerConfig()) {}

  Upstream::MockClusterManager cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::MockStore store_;
  Stats::MockScope& scope_{store_.mockScope()};
  DangerousDeprecatedTestTime test_time_;
  Api::ApiPtr api_;
  StatNames stat_names_;
  AsyncClientManagerImpl async_client_manager_;
};

void testGetOrCreateAsyncClientWithConfig(::benchmark::State& state) {
  AsyncClientManagerImplTest async_client_man_test;

  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < 1000; i++) {
      RawAsyncClientSharedPtr foo_client0 =
          async_client_man_test.async_client_manager_.getOrCreateRawAsyncClient(
              grpc_service, async_client_man_test.scope_, true);
    }
  }
}

void testGetOrCreateAsyncClientWithHashConfig(::benchmark::State& state) {
  AsyncClientManagerImplTest async_client_man_test;

  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  GrpcServiceConfigWithHashKey config_with_hash_key_a = GrpcServiceConfigWithHashKey(grpc_service);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    for (int i = 0; i < 1000; i++) {
      RawAsyncClientSharedPtr foo_client0 =
          async_client_man_test.async_client_manager_.getOrCreateRawAsyncClientWithHashKey(
              config_with_hash_key_a, async_client_man_test.scope_, true);
    }
  }
}

BENCHMARK(testGetOrCreateAsyncClientWithConfig)->Unit(::benchmark::kMicrosecond);
BENCHMARK(testGetOrCreateAsyncClientWithHashConfig)->Unit(::benchmark::kMicrosecond);

} // namespace
} // namespace Grpc
} // namespace Envoy
