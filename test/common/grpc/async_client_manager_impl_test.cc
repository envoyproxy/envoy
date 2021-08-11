#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/api/api_impl.h"
#include "source/common/grpc/async_client_manager_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class AsyncClientManagerImplTest : public testing::Test {
public:
  AsyncClientManagerImplTest()
      : api_(Api::createApiForTest()), stat_names_(scope_.symbolTable()),
        async_client_manager_(cm_, tls_, test_time_.timeSystem(), *api_, stat_names_) {}

  Upstream::MockClusterManager cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::MockStore scope_;
  DangerousDeprecatedTestTime test_time_;
  Api::ApiPtr api_;
  StatNames stat_names_;
  AsyncClientManagerImpl async_client_manager_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMaps cluster_maps;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_maps.active_clusters_.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_maps));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi());

  async_client_manager_.factoryForGrpcService(grpc_service, scope_, false);
}

TEST_F(AsyncClientManagerImplTest, DisableRawAsyncClientCache) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  RawAsyncClientSharedPtr foo_client0 = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::AlwaysCache);

  RawAsyncClientSharedPtr uncached_foo_client = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::CacheWhenRuntimeEnabled);
  // Raw async client is not cached when runtime is disabled.
  EXPECT_NE(foo_client0.get(), uncached_foo_client.get());

  RawAsyncClientSharedPtr foo_client1 = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::AlwaysCache);
  // When cache option is AlwaysCache, will use cache even when runtime is disabled.
  EXPECT_EQ(foo_client0.get(), foo_client1.get());
}

TEST_F(AsyncClientManagerImplTest, EnableRawAsyncClientCache) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.enable_grpc_async_client_cache", "true"}});
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  // Use cache when runtime is enabled.
  RawAsyncClientSharedPtr foo_client0 = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::CacheWhenRuntimeEnabled);
  RawAsyncClientSharedPtr foo_client1 = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::CacheWhenRuntimeEnabled);
  EXPECT_EQ(foo_client0.get(), foo_client1.get());

  // Get a different raw async client with different cluster config.
  grpc_service.mutable_envoy_grpc()->set_cluster_name("bar");
  RawAsyncClientSharedPtr bar_client = async_client_manager_.getOrCreateRawAsyncClient(
      grpc_service, scope_, true, CacheOption::CacheWhenRuntimeEnabled);
  EXPECT_NE(foo_client1.get(), bar_client.get());
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknown) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters());
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Unknown gRPC client cluster 'foo'");
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcDynamicCluster) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters())
      .WillOnce(Return(Upstream::ClusterManager::ClusterInfoMaps{cluster_map, {}}));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "gRPC client cluster 'foo' is not static");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager_.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInKey) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("illegalcharacter;");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Illegal characters in gRPC initial metadata header key: illegalcharacter;.");
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, LegalGoogleGrpcChar) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("_legal-character.");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager_.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInValue) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("legal-key");
  metadata.set_value("NonAsciValue.भारत");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Illegal ASCII value for gRPC initial metadata header key: legal-key.");
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknownOk) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters()).Times(0);
  ASSERT_NO_THROW(async_client_manager_.factoryForGrpcService(grpc_service, scope_, true));
}

} // namespace
} // namespace Grpc
} // namespace Envoy
