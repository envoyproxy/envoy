#include "common/upstream/logical_host.h"

#include "test/mocks/upstream/cluster_info.h"

using testing::InSequence;

namespace Envoy {
namespace Upstream {

TEST(ClusterInfoWrapperTest, PassThrough) {
  InSequence s;

  auto cluster_info = std::make_shared<MockClusterInfo>();
  ClusterInfoWrapper wrapper(cluster_info);

  EXPECT_CALL(*cluster_info, addedViaApi());
  wrapper.addedViaApi();
  EXPECT_CALL(*cluster_info, connectTimeout());
  wrapper.connectTimeout();
  EXPECT_CALL(*cluster_info, idleTimeout());
  wrapper.idleTimeout();
  EXPECT_CALL(*cluster_info, perConnectionBufferLimitBytes());
  wrapper.perConnectionBufferLimitBytes();
  EXPECT_CALL(*cluster_info, features());
  wrapper.features();
  EXPECT_CALL(*cluster_info, http2Settings());
  wrapper.http2Settings();
  EXPECT_CALL(*cluster_info, lbConfig());
  wrapper.lbConfig();
  EXPECT_CALL(*cluster_info, lbType());
  wrapper.lbType();
  EXPECT_CALL(*cluster_info, type());
  wrapper.type();
  EXPECT_CALL(*cluster_info, clusterType());
  wrapper.clusterType();
  EXPECT_CALL(*cluster_info, lbLeastRequestConfig());
  wrapper.lbLeastRequestConfig();
  EXPECT_CALL(*cluster_info, lbRingHashConfig());
  wrapper.lbRingHashConfig();
  EXPECT_CALL(*cluster_info, lbOriginalDstConfig());
  wrapper.lbOriginalDstConfig();
  EXPECT_CALL(*cluster_info, maintenanceMode());
  wrapper.maintenanceMode();
  EXPECT_CALL(*cluster_info, maxRequestsPerConnection());
  wrapper.maxRequestsPerConnection();
  EXPECT_CALL(*cluster_info, name());
  wrapper.name();
  EXPECT_CALL(*cluster_info, resourceManager(ResourcePriority::High));
  wrapper.resourceManager(ResourcePriority::High);
  EXPECT_CALL(*cluster_info, transportSocketFactory());
  wrapper.transportSocketFactory();
  EXPECT_CALL(*cluster_info, stats());
  wrapper.stats();
  EXPECT_CALL(*cluster_info, statsScope());
  wrapper.statsScope();
  EXPECT_CALL(*cluster_info, loadReportStats());
  wrapper.loadReportStats();
  EXPECT_CALL(*cluster_info, sourceAddress());
  wrapper.sourceAddress();
  EXPECT_CALL(*cluster_info, lbSubsetInfo());
  wrapper.lbSubsetInfo();
  EXPECT_CALL(*cluster_info, metadata());
  wrapper.metadata();
  EXPECT_CALL(*cluster_info, typedMetadata());
  wrapper.typedMetadata();
  EXPECT_CALL(*cluster_info, clusterSocketOptions());
  wrapper.clusterSocketOptions();
  EXPECT_CALL(*cluster_info, drainConnectionsOnHostRemoval());
  wrapper.drainConnectionsOnHostRemoval();
  EXPECT_CALL(*cluster_info, warmHosts());
  wrapper.warmHosts();
  EXPECT_CALL(*cluster_info, eds_service_name());
  wrapper.eds_service_name();
  EXPECT_CALL(*cluster_info, extensionProtocolOptions("foo"));
  wrapper.extensionProtocolOptions("foo");
}

} // namespace Upstream
} // namespace Envoy
