#include "common/api/api_impl.h"
#include "common/grpc/async_client_manager_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
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
  AsyncClientManagerImplTest() : api_(Api::createApiForTest(api_stats_store_)) {}

  Upstream::MockClusterManager cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::IsolatedStoreImpl api_stats_store_;
  Stats::MockStore scope_;
  DangerousDeprecatedTestTime test_time_;
  Api::ApiPtr api_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_, test_time_.timeSystem(), *api_);
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi());

  async_client_manager.factoryForGrpcService(grpc_service, scope_, false);
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknown) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_, test_time_.timeSystem(), *api_);
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters());
  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_, false),
                            EnvoyException, "Unknown gRPC client cluster 'foo'");
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcDynamicCluster) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_, test_time_.timeSystem(), *api_);
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_, false),
                            EnvoyException, "gRPC client cluster 'foo' is not static");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  AsyncClientManagerImpl async_client_manager(cm_, tls_, test_time_.timeSystem(), *api_);
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_, false),
                            EnvoyException, "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknownOk) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_, test_time_.timeSystem(), *api_);
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters()).Times(0);
  ASSERT_NO_THROW(async_client_manager.factoryForGrpcService(grpc_service, scope_, true));
}

} // namespace
} // namespace Grpc
} // namespace Envoy
