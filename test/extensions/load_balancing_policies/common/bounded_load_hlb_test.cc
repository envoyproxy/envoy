#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/extensions/load_balancing_policies/common/thread_aware_lb_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Upstream {
namespace {

class TestHashingLoadBalancer : public ThreadAwareLoadBalancerBase::HashingLoadBalancer {
public:
  explicit TestHashingLoadBalancer(NormalizedHostWeightVector ring) : ring_(std::move(ring)) {}
  HostConstSharedPtr chooseHost(uint64_t hash, uint32_t /* attempt */) const override {
    if (ring_.empty()) {
      return nullptr;
    }
    return ring_.at(hash).first;
  }

private:
  const NormalizedHostWeightVector ring_;
};

using HostOverloadFactorPredicate = std::function<double(const Host& host, double weight)>;
class TestBoundedLoadHashingLoadBalancer
    : public ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer {
public:
  TestBoundedLoadHashingLoadBalancer(
      ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb_ptr,
      const NormalizedHostWeightVector& normalized_host_weights, uint32_t hash_balance_factor,
      HostOverloadFactorPredicate host_overload_factor)
      : ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer(
            hlb_ptr, normalized_host_weights, hash_balance_factor),
        host_overload_factor_(host_overload_factor) {}

private:
  HostOverloadFactorPredicate host_overload_factor_;
  double hostOverloadFactor(const Host& host, double weight) const override {
    if (host_overload_factor_ == nullptr) {
      return ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer::hostOverloadFactor(
          host, weight);
    }
    return host_overload_factor_(host, weight);
  }
};

class BoundedLoadHashingLoadBalancerTest : public Event::TestUsingSimulatedTime,
                                           public testing::Test {
public:
  HostOverloadFactorPredicate
  getHostOverloadFactorPredicate(const std::vector<std::string>& addresses) {
    return [addresses](const Host& h, double) -> double {
      // Each host in the vector gets an increasing overload factor.
      int index = 1;
      for (const std::string& host : addresses) {
        index = index + 1;
        if (host == h.address()->asString()) {
          return (100.0 + index) / 100;
        }
      }
      return 0.5;
    };
  }

  void createHosts(uint32_t num_hosts, NormalizedHostWeightVector& normalized_host_weights) {
    const double equal_weight = static_cast<double>(1.0 / num_hosts);
    for (uint32_t i = 0; i < num_hosts; i++) {
      normalized_host_weights.push_back(
          {makeTestHost(info_, fmt::format("tcp://127.0.0.1{}:90", i), simTime()), equal_weight});
    }
  }

  // creates hosts and also puts them in a ring so that 2 hashes map to the same host.
  void createHosts(uint32_t num_hosts, NormalizedHostWeightVector& hosts,
                   NormalizedHostWeightVector& ring) {
    const double equal_weight = static_cast<double>(1.0 / num_hosts);
    for (uint32_t i = 0; i < num_hosts; i++) {
      HostConstSharedPtr h = makeTestHost(info_, fmt::format("tcp://127.0.0.1{}:90", i), simTime());
      ring.push_back({h, equal_weight});
      ring.push_back({h, equal_weight});
      hosts.push_back({h, equal_weight});
    }
  }

  ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb_;
  std::unique_ptr<ThreadAwareLoadBalancerBase::BoundedLoadHashingLoadBalancer> lb_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};

  HostOverloadFactorPredicate host_overload_factor_predicate_;
};

class HashingLoadBalancerTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ThreadAwareLoadBalancerBase::HashingLoadBalancerSharedPtr hlb_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
};

// Test hashKey for host
TEST_F(HashingLoadBalancerTest, HashKey) {
  NormalizedHostWeightVector normalized_host_weights;
  hlb_ = std::make_shared<TestHashingLoadBalancer>(normalized_host_weights);

  HostSharedPtr host = makeTestHost(info_, "hostname", "tcp://127.0.0.1:90", simTime());
  // don't use hostname
  EXPECT_EQ(hlb_->hashKey(host, false), "127.0.0.1:90");
  // use hostname
  EXPECT_EQ(hlb_->hashKey(host, true), "hostname");

  // string metadata
  envoy::config::core::v3::Metadata string_metadata;
  Config::Metadata::mutableMetadataValue(string_metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().HASH_KEY)
      .set_string_value("hash_key");
  host = makeTestHostWithMetadata(
      info_, std::make_shared<const envoy::config::core::v3::Metadata>(string_metadata),
      "tcp://127.0.0.1:90", simTime());
  EXPECT_EQ(hlb_->hashKey(host, false), "hash_key");

  // other type(int) metadata
  envoy::config::core::v3::Metadata int_metadata;
  Config::Metadata::mutableMetadataValue(int_metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().HASH_KEY)
      .set_number_value(1337);
  host = makeTestHostWithMetadata(
      info_, std::make_shared<const envoy::config::core::v3::Metadata>(int_metadata),
      "tcp://127.0.0.1:90", simTime());
  EXPECT_EQ(hlb_->hashKey(host, false), "127.0.0.1:90");
};

// Works correctly without any hosts.
TEST_F(BoundedLoadHashingLoadBalancerTest, NoHosts) {
  NormalizedHostWeightVector normalized_host_weights;
  hlb_ = std::make_shared<TestHashingLoadBalancer>(normalized_host_weights);
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             nullptr);
  EXPECT_EQ(lb_->chooseHost(1, 1), nullptr);
};

// Works correctly for the case when no host is ever overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, NoHostEverOverloaded) {
  // setup: 5 hosts, none ever overloaded.
  std::vector<std::string> addresses;
  host_overload_factor_predicate_ = getHostOverloadFactorPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVector ring(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             host_overload_factor_predicate_);

  for (uint32_t i = 0; i < 5; i++) {
    HostConstSharedPtr host = lb_->chooseHost(i, 1);
    EXPECT_NE(host, nullptr);
    EXPECT_EQ(host->address()->asString(), fmt::format("127.0.0.1{}:90", i));
  }
};

TEST_F(BoundedLoadHashingLoadBalancerTest, ActualHostOverloaded) {
  // The random shuffle sequence of 5 elements with seed 2 is 2 1 0 4 3.

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVector ring(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);
  // To use actual host overload factor.
  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             nullptr);
  HostConstSharedPtr host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.12:90");

  // Mock the host to be overloaded.
  host->cluster().trafficStats()->upstream_cx_active_.add(2);
  host->stats().rq_active_.add(2);

  // The host is overloaded, so the next host in the sequence is picked up.
  host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.11:90");
};

// Works correctly for the case one host is overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, OneHostOverloaded) {
  // In this test host 2 is overloaded. The random shuffle sequence of 5
  // elements with seed 2 is 2 1 0 4 3. When the host picked up for
  // hash 2 (which is 127.0.0.12) is overloaded, host 1 (127.0.0.11)
  // is picked up.

  // setup: 5 hosts, one of them is overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.12:90");
  host_overload_factor_predicate_ = getHostOverloadFactorPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVector ring(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             host_overload_factor_predicate_);

  HostConstSharedPtr host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.11:90");
};

// Works correctly for the case a few hosts are overloaded.
TEST_F(BoundedLoadHashingLoadBalancerTest, MultipleHostOverloaded) {
  // In this test hosts 0, 1 & 2 are overloaded. The random shuffle
  // sequence of 5 elements with seed 2 is 2 1 0 4 3. When the host
  // picked up for hash 2 (which is 127.0.0.12) is overloaded, the
  // method passes over host 0 and picks host 3 (127.0.0.13) up.

  // setup: 5 hosts, few of them are overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.10:90");
  addresses.push_back("127.0.0.11:90");
  addresses.push_back("127.0.0.12:90");
  host_overload_factor_predicate_ = getHostOverloadFactorPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(5, normalized_host_weights);

  NormalizedHostWeightVector ring(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             host_overload_factor_predicate_);

  HostConstSharedPtr host = lb_->chooseHost(2, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.14:90");
};

// Works correctly for the case when requests with different hash map to the same
// overloaded host.
TEST_F(BoundedLoadHashingLoadBalancerTest, MultipleHashSameHostOverloaded) {
  // In this case host 3 is overloaded and the CH ring has same host repeated on
  // consecutive indices (0 0 1 1 2 2 3 3 4 4). The hashes 6 and 7 map to same host
  // 3 which is overloaded. The random shuffle sequence of 5 elements with seed 6 is
  // 4 0 2 3 1 and with 7 it is 0 1 4 3 2. Hence hosts 4 and 0 are picked up for these
  // hashes.

  // setup: 5 hosts, one of them is overloaded.
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.13:90");
  host_overload_factor_predicate_ = getHostOverloadFactorPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights, hosts_on_ring;
  createHosts(5, normalized_host_weights, hosts_on_ring);

  hlb_ = std::make_shared<TestHashingLoadBalancer>(hosts_on_ring);

  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             host_overload_factor_predicate_);

  HostConstSharedPtr host1 = lb_->chooseHost(6, 1);
  EXPECT_NE(host1, nullptr);
  HostConstSharedPtr host2 = lb_->chooseHost(7, 1);
  EXPECT_NE(host2, nullptr);

  EXPECT_NE(host1->address()->asString(), host2->address()->asString());

  // sequence for 4 is 40231, 4 is the first host not overloaded
  EXPECT_EQ(host1->address()->asString(), "127.0.0.14:90");
  // sequence for 5 is 01432, 0 is the first host not overloaded
  EXPECT_EQ(host2->address()->asString(), "127.0.0.10:90");
};

// Works correctly for the case when all hosts are overloaded
TEST_F(BoundedLoadHashingLoadBalancerTest, AllHostsOverloaded) {
  std::vector<std::string> addresses;
  addresses.push_back("127.0.0.11:90");
  addresses.push_back("127.0.0.10:90");
  addresses.push_back("127.0.0.12:90");
  host_overload_factor_predicate_ = getHostOverloadFactorPredicate(addresses);

  NormalizedHostWeightVector normalized_host_weights;
  createHosts(3, normalized_host_weights);

  NormalizedHostWeightVector ring(normalized_host_weights);
  hlb_ = std::make_shared<TestHashingLoadBalancer>(ring);

  lb_ = std::make_unique<TestBoundedLoadHashingLoadBalancer>(hlb_, normalized_host_weights, 1,
                                                             host_overload_factor_predicate_);

  HostConstSharedPtr host = lb_->chooseHost(0, 1);
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.11:90");
};

} // namespace
} // namespace Upstream
} // namespace Envoy
