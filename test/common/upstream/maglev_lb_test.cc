#include "common/upstream/maglev_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override { return hash_key_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }

  Optional<uint64_t> hash_key_;
};

// Note: ThreadAwareLoadBalancer base is heavily tested by RingHashLoadBalancerTest. Only basic
//       functionality is covered here.
class MaglevLoadBalancerTest : public ::testing::Test {
public:
  MaglevLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init() {
    lb_.reset(new MaglevLoadBalancer(priority_set_, stats_, runtime_, random_, common_config_, 7));
    lb_->initialize();
  }

  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<MaglevLoadBalancer> lb_;
};

// Works correctly without any hosts.
TEST_F(MaglevLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

// Basic sanity tests.
TEST_F(MaglevLoadBalancerTest, Basic) {
  host_set_.hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  init();

  // maglev: i=0 host=127.0.0.1:92
  // maglev: i=1 host=127.0.0.1:94
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:95
  // maglev: i=5 host=127.0.0.1:90
  // maglev: i=6 host=127.0.0.1:93
  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(host_set_.hosts_[2], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(15);
    EXPECT_EQ(host_set_.hosts_[4], lb->chooseHost(&context));
  }
}

} // namespace Upstream
} // namespace Envoy
