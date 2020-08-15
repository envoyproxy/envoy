#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/upstream/thread_aware_lb_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Upstream {
namespace {

class TestHashingLoadBalancer : public ThreadAwareLoadBalancerBase::HashingLoadBalancer {
public:
  TestHashingLoadBalancer(NormalizedHostWeightVectorConstPtr& ring) : ring_(std::move(ring)) {}
  TestHashingLoadBalancer() : ring_(nullptr) {}
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t /* attempt */) const {
    if (ring_ == nullptr) {
      return nullptr;
    }
    return ring_->at(hash).first;
  }
  NormalizedHostWeightVectorConstPtr ring_;

private:
};

using HostOverloadedPredicate = std::function<bool(HostConstSharedPtr host, double weight)>;
class TestBoundedLoadHashingLoadBalancer
    : public ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer {
public:
  TestBoundedLoadHashingLoadBalancer(
      ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb_ptr,
      NormalizedHostWeightVectorConstPtr& normalized_host_weights_ptr, uint32_t hash_balance_factor,
      HostOverloadedPredicate is_host_overloaded)
      : ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer(
            hlb_ptr, normalized_host_weights_ptr, hash_balance_factor),
        is_host_overloaded_(is_host_overloaded) {}

private:
  HostOverloadedPredicate is_host_overloaded_;
  bool isHostOverloaded(HostConstSharedPtr host, double weight) const override {
    return is_host_overloaded_(host, weight);
  }
};

class BoundedLoadHashingLoadBalancerTest : public testing::Test {
public:
  HostOverloadedPredicate getHostOverloadedPredicate(HostConstSharedPtr overloaded_host) {
    HostConstSharedPtr host = overloaded_host;
    return [&host](HostConstSharedPtr h, double) -> bool { return h == host; };
  }

  HostOverloadedPredicate getHostOverloadedPredicate(const std::vector<std::string> addresses) {
    std::vector<std::string> hosts_overloaded = addresses;
    return [hosts_overloaded](HostConstSharedPtr h, double) -> bool {
      for (std::string host : hosts_overloaded) {
        if (host.compare(h->address()->asString()) == 0) {
          return true;
        }
      }
      return false;
    };
  }

  void createHosts(uint32_t num_hosts, NormalizedHostWeightVector& normalized_host_weights) {
    const double equal_weight = static_cast<double>(1.0 / num_hosts);
    for (uint32_t i = 0; i < num_hosts; i++) {
      normalized_host_weights.push_back(
          {makeTestHost(info_, fmt::format("tcp://127.0.0.1{}:90", i)), equal_weight});
    }
  }

  void createHostsMappedByMultipleHosts(uint32_t num_hosts, NormalizedHostWeightVector& hosts,
                                        NormalizedHostWeightVector& ring) {
    const double equal_weight = static_cast<double>(1.0 / num_hosts);
    for (uint32_t i = 0; i < num_hosts; i++) {
      HostConstSharedPtr h = makeTestHost(info_, fmt::format("tcp://127.0.0.1{}:90", i));
      ring.push_back({h, equal_weight});
      ring.push_back({h, equal_weight});
      hosts.push_back({h, equal_weight});
    }
  }

  ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb_;
  std::unique_ptr<ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer> lb_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};

  HostOverloadedPredicate hostOverLoadedPredicate_;
};

// Works correctly when hash balance factor is 0, when balancing is not required.
TEST_F(BoundedLoadHashingLoadBalancerTest, HashBalanceDisabled) {
  ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb =
      std::make_shared<TestHashingLoadBalancer>();
  NormalizedHostWeightVectorConstPtr ptr = nullptr;
  EXPECT_DEATH(std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb, ptr, 0, nullptr), "");
};

// Works correctly without any hosts (nullptr or empty vector).
TEST_F(BoundedLoadHashingLoadBalancerTest, NoHosts) {
  hlb_ = std::make_shared<TestHashingLoadBalancer>();
  NormalizedHostWeightVectorConstPtr ptr = nullptr;
  EXPECT_DEATH(std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, ptr, 1, nullptr), "");

  NormalizedHostWeightVectorConstPtr normalized_host_weights =
      std::make_unique<NormalizedHostWeightVector>();
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             nullptr);
  EXPECT_EQ(lb_->chooseHost(1, 1), nullptr);
};

// Works correctly without any hashing load balancer.
TEST_F(BoundedLoadHashingLoadBalancerTest, NoHashingLoadBalancer) {
  NormalizedHostWeightVectorConstPtr normalized_host_weights =
      std::make_unique<NormalizedHostWeightVector>();
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(nullptr, normalized_host_weights, 1,
                                                             nullptr);

  EXPECT_EQ(lb_->chooseHost(1, 1), nullptr);
};

// Works correctly for the case when no host is ever overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, NoHostEverOverloaded) {
  // setup: 5 hosts, none ever overloaded.
  std::vector<std::string> addresses;
  hostOverLoadedPredicate_ = getHostOverloadedPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVectorConstPtr ring =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  NormalizedHostWeightVectorConstPtr constPtr =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, constPtr, 1,
                                                             hostOverLoadedPredicate_);

  // test
  for (uint32_t i = 0; i < 5; i++) {
    HostConstSharedPtr host = lb_->chooseHost(i, 1);
    EXPECT_NE(host, nullptr);
    EXPECT_EQ(host->address()->asString(), fmt::format("127.0.0.1{}:90", i));
  }
};

// Works correctly for the case one host is overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, OneHostOverloaded) {
  /*
    In this host 2 is overloaded. The random shuffle sequence of 5
    elements with seed 2 is 3 1 4 0 2. When the host picked up for
    hash 2 (which is 127.0.0.12) is overloaded, host 3 (127.0.0.13)
    is picked up.
  */

  // setup: 5 hosts, one of them is overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.12:90");
  hostOverLoadedPredicate_ = getHostOverloadedPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVectorConstPtr ring =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  NormalizedHostWeightVectorConstPtr normalized_host_weights_ptr =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights_ptr, 1,
                                                             hostOverLoadedPredicate_);

  // test
  HostConstSharedPtr host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.10:90");
};

// Works correctly for the case a few hosts are overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, MultipleHostOverloaded) {
  /*
    In this case hosts 1, 2 & 3 are overloaded. The random shuffle
    sequence of 5 elements with seed 2 is 3 1 4 0 2. When the host
    picked up for hash 2 (which is 127.0.0.12) is overloaded, the
    method passes over hosts 3 & 1 and picks host 4 (127.0.0.14)
    is picked up.

  */

  // setup: 5 hosts, few of them are overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.11:90");
  addresses.push_back("127.0.0.12:90");
  addresses.push_back("127.0.0.10:90");
  hostOverLoadedPredicate_ = getHostOverloadedPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVectorConstPtr ring =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  NormalizedHostWeightVectorConstPtr normalized_host_weights_ptr =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights_ptr, 1,
                                                             hostOverLoadedPredicate_);

  // test
  HostConstSharedPtr host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.14:90");
};

// Works correctly for the case a few hosts are overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, MultipleHashSameHostOverloaded) {
  /*
    In this case host 2 is overloaded. The random shuffle
    sequence of 5 elements with seed 2 is 3 1 4 0 2. When the host
    picked up for hash 2 (which is 127.0.0.12) is overloaded, the
    method passes over hosts 3 & 1 and picks host 4 (127.0.0.10)
    is picked up,

  */
  // setup: 5 hosts, one of them is overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.13:90");
  hostOverLoadedPredicate_ = getHostOverloadedPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights, hosts_on_ring;
  createHostsMappedByMultipleHosts(5, normalized_host_weights, hosts_on_ring);

  NormalizedHostWeightVectorConstPtr ring =
      std::make_unique<const NormalizedHostWeightVector>(hosts_on_ring);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  NormalizedHostWeightVectorConstPtr normalized_host_weights_ptr =
      std::make_unique<const NormalizedHostWeightVector>(normalized_host_weights);
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights_ptr, 1,
                                                             hostOverLoadedPredicate_);

  // test
  HostConstSharedPtr host1 = lb_->chooseHost(6, 1);
  EXPECT_NE(host1, nullptr);
  HostConstSharedPtr host2 = lb_->chooseHost(7, 1);
  EXPECT_NE(host2, nullptr);

  // they are different
  EXPECT_NE(host1->address()->asString(), host2->address()->asString());

  // sequence for 4 is 34021;
  EXPECT_EQ(host1->address()->asString(), "127.0.0.12:90");
  // sequence for 5 is 20134
  EXPECT_EQ(host2->address()->asString(), "127.0.0.14:90");
};

} // namespace
} // namespace Upstream
} // namespace Envoy
