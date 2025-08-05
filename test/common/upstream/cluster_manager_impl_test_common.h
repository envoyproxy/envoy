#pragma once

#include <string>
#include <vector>

#include "envoy/common/matchers.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/dns_resolver.h"
#include "envoy/upstream/host_description.h"

#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/router/context_impl.h"

#include "test/common/upstream/test_cluster_manager.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/config/xds_manager.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using ::envoy::config::bootstrap::v3::Bootstrap;
using ::testing::NiceMock;

Bootstrap parseBootstrapFromV3Yaml(const std::string& yaml);
Bootstrap defaultConfig();
std::string clustersJson(const std::vector<std::string>& clusters);

class HttpPoolDataPeer {
public:
  static Http::ConnectionPool::MockInstance* getPool(absl::optional<HttpPoolData> data) {
    ASSERT(data.has_value());
    return dynamic_cast<Http::ConnectionPool::MockInstance*>(data.value().pool_);
  }
};

class TcpPoolDataPeer {
public:
  static Tcp::ConnectionPool::MockInstance* getPool(absl::optional<TcpPoolData> data) {
    ASSERT(data.has_value());
    return dynamic_cast<Tcp::ConnectionPool::MockInstance*>(data.value().pool_);
  }
};

// Helper to intercept calls to postThreadLocalClusterUpdate.
class MockLocalClusterUpdate {
public:
  MOCK_METHOD(void, post,
              (uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed));
};

class MockLocalHostsRemoved {
public:
  MOCK_METHOD(void, post, (const HostVector&));
};

class ClusterManagerImplTest : public testing::Test {
public:
  ClusterManagerImplTest();

  virtual void create(const Bootstrap& bootstrap);

  void createWithBasicStaticCluster();

  void createWithLocalClusterUpdate(const bool enable_merge_window = true);

  void checkStats(uint64_t added, uint64_t modified, uint64_t removed, uint64_t active,
                  uint64_t warming);

  void
  checkConfigDump(const std::string& expected_dump_yaml,
                  const Matchers::StringMatcher& name_matcher = Matchers::UniversalStringMatcher());

  MetadataConstSharedPtr buildMetadata(const std::string& version) const;

  Event::SimulatedTimeSystem time_system_;
  NiceMock<TestClusterManagerFactory> factory_;
  std::unique_ptr<TestClusterManagerImpl> cluster_manager_;
  MockLocalClusterUpdate local_cluster_update_;
  MockLocalHostsRemoved local_hosts_removed_;
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;
};

} // namespace Upstream
} // namespace Envoy
