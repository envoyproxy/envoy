#include "common/grpc/async_client_manager_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class AsyncClientManagerImplTest : public testing::Test {
public:
  Upstream::MockClusterManager cm_;
  ThreadLocal::MockInstance tls_;
  Stats::MockStore scope_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_);
  envoy::api::v2::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi());

  async_client_manager.factoryForGrpcService(grpc_service, scope_);
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknown) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_);
  envoy::api::v2::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, clusters());
  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_),
                            EnvoyException, "Unknown gRPC client cluster 'foo'");
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcDynamicCluster) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_);
  envoy::api::v2::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("foo", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_),
                            EnvoyException, "gRPC client cluster 'foo' is not static");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  AsyncClientManagerImpl async_client_manager(cm_, tls_);
  envoy::api::v2::GrpcService grpc_service;
  grpc_service.mutable_google_grpc();

  EXPECT_THROW_WITH_MESSAGE(async_client_manager.factoryForGrpcService(grpc_service, scope_),
                            EnvoyException, "Google C++ gRPC client is not implemented yet");
}

} // namespace
} // namespace Grpc
} // namespace Envoy
