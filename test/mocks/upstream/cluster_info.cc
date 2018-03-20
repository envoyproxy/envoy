#include "test/mocks/upstream/cluster_info.h"

#include "common/network/raw_buffer_socket.h"
#include "common/upstream/upstream_impl.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Upstream {

MockLoadBalancerSubsetInfo::MockLoadBalancerSubsetInfo() {
  ON_CALL(*this, isEnabled()).WillByDefault(Return(false));
  ON_CALL(*this, fallbackPolicy())
      .WillByDefault(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  ON_CALL(*this, defaultSubset()).WillByDefault(ReturnRef(ProtobufWkt::Struct::default_instance()));
  ON_CALL(*this, subsetKeys()).WillByDefault(ReturnRef(subset_keys_));
}

MockLoadBalancerSubsetInfo::~MockLoadBalancerSubsetInfo() {}

MockIdleTimeEnabledClusterInfo::MockIdleTimeEnabledClusterInfo() {

  ON_CALL(*this, idleTimeout()).WillByDefault(Return(std::chrono::milliseconds(1000)));
}

MockIdleTimeEnabledClusterInfo::~MockIdleTimeEnabledClusterInfo() {}

MockClusterInfo::MockClusterInfo()
    : stats_(ClusterInfoImpl::generateStats(stats_store_)),
      transport_socket_factory_(new Network::RawBufferSocketFactory),
      load_report_stats_(ClusterInfoImpl::generateLoadReportStats(load_report_stats_store_)),
      resource_manager_(new Upstream::ResourceManagerImpl(runtime_, "fake_key", 1, 1024, 1024, 1)) {

  ON_CALL(*this, connectTimeout()).WillByDefault(Return(std::chrono::milliseconds(1)));
  ON_CALL(*this, idleTimeout()).WillByDefault(Return(absl::optional<std::chrono::milliseconds>()));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, http2Settings()).WillByDefault(ReturnRef(http2_settings_));
  ON_CALL(*this, maxRequestsPerConnection())
      .WillByDefault(ReturnPointee(&max_requests_per_connection_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, statsScope()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, transportSocketFactory()).WillByDefault(ReturnRef(*transport_socket_factory_));
  ON_CALL(*this, loadReportStats()).WillByDefault(ReturnRef(load_report_stats_));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, resourceManager(_))
      .WillByDefault(Invoke(
          [this](ResourcePriority) -> Upstream::ResourceManager& { return *resource_manager_; }));
  ON_CALL(*this, lbType()).WillByDefault(ReturnPointee(&lb_type_));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, lbSubsetInfo()).WillByDefault(ReturnRef(lb_subset_));
  ON_CALL(*this, lbRingHashConfig()).WillByDefault(ReturnRef(lb_ring_hash_config_));
  ON_CALL(*this, lbConfig()).WillByDefault(ReturnRef(lb_config_));
}

MockClusterInfo::~MockClusterInfo() {}

} // namespace Upstream
} // namespace Envoy
