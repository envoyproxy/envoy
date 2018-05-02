#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/cds.pb.h"

#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/upstream/subset_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::EndsWith;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Upstream {

class SubsetLoadBalancerDescribeMetadataTester {
public:
  SubsetLoadBalancerDescribeMetadataTester(std::shared_ptr<SubsetLoadBalancer> lb) : lb_(lb) {}

  typedef std::vector<std::pair<std::string, ProtobufWkt::Value>> MetadataVector;

  void test(std::string expected, const MetadataVector& metadata) {
    SubsetLoadBalancer::SubsetMetadata subset_metadata(metadata);
    EXPECT_EQ(expected, lb_.get()->describeMetadata(subset_metadata));
  }

private:
  std::shared_ptr<SubsetLoadBalancer> lb_;
};

namespace SubsetLoadBalancerTest {

class TestMetadataMatchCriterion : public Router::MetadataMatchCriterion {
public:
  TestMetadataMatchCriterion(const std::string& name, const HashedValue& value)
      : name_(name), value_(value) {}

  const std::string& name() const override { return name_; }
  const HashedValue& value() const override { return value_; }

private:
  std::string name_;
  HashedValue value_;
};

class TestMetadataMatchCriteria : public Router::MetadataMatchCriteria {
public:
  TestMetadataMatchCriteria(const std::map<std::string, std::string> matches) {
    for (const auto& it : matches) {
      ProtobufWkt::Value v;
      v.set_string_value(it.second);

      matches_.emplace_back(
          std::make_shared<const TestMetadataMatchCriterion>(it.first, HashedValue(v)));
    }
  }

  const std::vector<Router::MetadataMatchCriterionConstSharedPtr>&
  metadataMatchCriteria() const override {
    return matches_;
  }

private:
  std::vector<Router::MetadataMatchCriterionConstSharedPtr> matches_;
};

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(
      std::initializer_list<std::map<std::string, std::string>::value_type> metadata_matches)
      : matches_(
            new TestMetadataMatchCriteria(std::map<std::string, std::string>(metadata_matches))) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return {}; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return matches_.get();
  }

private:
  const std::shared_ptr<Router::MetadataMatchCriteria> matches_;
};

enum UpdateOrder { REMOVES_FIRST, SIMULTANEOUS };

class SubsetLoadBalancerTest : public testing::TestWithParam<UpdateOrder> {
public:
  SubsetLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  typedef std::map<std::string, std::string> HostMetadata;
  typedef std::map<std::string, HostMetadata> HostURLMetadataMap;

  void init() {
    init({
        {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
        {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
    });
  }

  void configureHostSet(const HostURLMetadataMap& host_metadata, MockHostSet& host_set) {
    HostVector hosts;
    for (const auto& it : host_metadata) {
      hosts.emplace_back(makeHost(it.first, it.second));
    }

    host_set.hosts_ = hosts;
    host_set.hosts_per_locality_ = makeHostsPerLocality({hosts});
    host_set.healthy_hosts_ = host_set.hosts_;
    host_set.healthy_hosts_per_locality_ = host_set.hosts_per_locality_;
  }

  void init(const HostURLMetadataMap& host_metadata) {
    HostURLMetadataMap failover;
    init(host_metadata, failover);
  }

  void init(const HostURLMetadataMap& host_metadata,
            const HostURLMetadataMap& failover_host_metadata) {
    EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

    configureHostSet(host_metadata, host_set_);
    if (!failover_host_metadata.empty()) {
      configureHostSet(failover_host_metadata, *priority_set_.getMockHostSet(1));
    }

    lb_.reset(new SubsetLoadBalancer(lb_type_, priority_set_, nullptr, stats_, runtime_, random_,
                                     subset_info_, ring_hash_lb_config_, common_config_));
  }

  void zoneAwareInit(const std::vector<HostURLMetadataMap>& host_metadata_per_locality,
                     const std::vector<HostURLMetadataMap>& local_host_metadata_per_locality) {
    EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

    HostVector hosts;
    std::vector<HostVector> hosts_per_locality;
    for (const auto& host_metadata : host_metadata_per_locality) {
      HostVector locality_hosts;
      for (const auto& host_entry : host_metadata) {
        HostSharedPtr host = makeHost(host_entry.first, host_entry.second);
        hosts.emplace_back(host);
        locality_hosts.emplace_back(host);
      }
      hosts_per_locality.emplace_back(locality_hosts);
    }

    host_set_.hosts_ = hosts;
    host_set_.hosts_per_locality_ = makeHostsPerLocality(std::move(hosts_per_locality));

    host_set_.healthy_hosts_ = host_set_.healthy_hosts_;
    host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;

    local_hosts_.reset(new HostVector());
    std::vector<HostVector> local_hosts_per_locality_vector;
    for (const auto& local_host_metadata : local_host_metadata_per_locality) {
      HostVector local_locality_hosts;
      for (const auto& host_entry : local_host_metadata) {
        HostSharedPtr host = makeHost(host_entry.first, host_entry.second);
        local_hosts_->emplace_back(host);
        local_locality_hosts.emplace_back(host);
      }
      local_hosts_per_locality_vector.emplace_back(local_locality_hosts);
    }
    local_hosts_per_locality_ = makeHostsPerLocality(std::move(local_hosts_per_locality_vector));

    local_priority_set_.getOrCreateHostSet(0).updateHosts(local_hosts_, local_hosts_,
                                                          local_hosts_per_locality_,
                                                          local_hosts_per_locality_, {}, {}, {});

    lb_.reset(new SubsetLoadBalancer(lb_type_, priority_set_, &local_priority_set_, stats_,
                                     runtime_, random_, subset_info_, ring_hash_lb_config_,
                                     common_config_));
  }

  HostSharedPtr makeHost(const std::string& url, const HostMetadata& metadata) {
    envoy::api::v2::core::Metadata m;
    for (const auto& m_it : metadata) {
      Config::Metadata::mutableMetadataValue(m, Config::MetadataFilters::get().ENVOY_LB, m_it.first)
          .set_string_value(m_it.second);
    }

    return makeTestHost(info_, url, m);
  }

  ProtobufWkt::Struct makeDefaultSubset(HostMetadata metadata) {
    ProtobufWkt::Struct default_subset;

    auto* fields = default_subset.mutable_fields();
    for (const auto& it : metadata) {
      ProtobufWkt::Value v;
      v.set_string_value(it.second);
      fields->insert({it.first, v});
    }

    return default_subset;
  }

  void modifyHosts(HostVector add, HostVector remove, absl::optional<uint32_t> add_in_locality = {},
                   uint32_t priority = 0) {
    MockHostSet& host_set = *priority_set_.getMockHostSet(priority);
    for (const auto& host : remove) {
      auto it = std::find(host_set.hosts_.begin(), host_set.hosts_.end(), host);
      if (it != host_set.hosts_.end()) {
        host_set.hosts_.erase(it);
      }
      host_set.healthy_hosts_ = host_set.hosts_;

      std::vector<HostVector> locality_hosts_copy = host_set.hosts_per_locality_->get();
      for (auto& locality_hosts : locality_hosts_copy) {
        auto it = std::find(locality_hosts.begin(), locality_hosts.end(), host);
        if (it != locality_hosts.end()) {
          locality_hosts.erase(it);
        }
      }
      host_set.hosts_per_locality_ = makeHostsPerLocality(std::move(locality_hosts_copy));
      host_set.healthy_hosts_per_locality_ = host_set.hosts_per_locality_;
    }

    if (GetParam() == REMOVES_FIRST && !remove.empty()) {
      host_set.runCallbacks({}, remove);
    }

    for (const auto& host : add) {
      host_set.hosts_.emplace_back(host);
      host_set.healthy_hosts_ = host_set.hosts_;

      if (add_in_locality) {
        std::vector<HostVector> locality_hosts_copy = host_set.hosts_per_locality_->get();
        locality_hosts_copy[add_in_locality.value()].emplace_back(host);
        host_set.hosts_per_locality_ = makeHostsPerLocality(std::move(locality_hosts_copy));
        host_set.healthy_hosts_per_locality_ = host_set.hosts_per_locality_;
      }
    }

    if (GetParam() == REMOVES_FIRST) {
      if (!add.empty()) {
        host_set_.runCallbacks(add, {});
      }
    } else if (!add.empty() || !remove.empty()) {
      host_set_.runCallbacks(add, remove);
    }
  }

  void modifyLocalHosts(HostVector add, HostVector remove, uint32_t add_in_locality) {
    for (const auto& host : remove) {
      auto it = std::find(local_hosts_->begin(), local_hosts_->end(), host);
      if (it != local_hosts_->end()) {
        local_hosts_->erase(it);
      }

      std::vector<HostVector> locality_hosts_copy = local_hosts_per_locality_->get();
      for (auto& locality_hosts : locality_hosts_copy) {
        auto it = std::find(locality_hosts.begin(), locality_hosts.end(), host);
        if (it != locality_hosts.end()) {
          locality_hosts.erase(it);
        }
      }
      local_hosts_per_locality_ = makeHostsPerLocality(std::move(locality_hosts_copy));
    }

    if (GetParam() == REMOVES_FIRST && !remove.empty()) {
      local_priority_set_.getOrCreateHostSet(0).updateHosts(
          local_hosts_, local_hosts_, local_hosts_per_locality_, local_hosts_per_locality_, {}, {},
          remove);
    }

    for (const auto& host : add) {
      local_hosts_->emplace_back(host);
      std::vector<HostVector> locality_hosts_copy = local_hosts_per_locality_->get();
      locality_hosts_copy[add_in_locality].emplace_back(host);
      local_hosts_per_locality_ = makeHostsPerLocality(std::move(locality_hosts_copy));
    }

    if (GetParam() == REMOVES_FIRST) {
      if (!add.empty()) {
        local_priority_set_.getOrCreateHostSet(0).updateHosts(
            local_hosts_, local_hosts_, local_hosts_per_locality_, local_hosts_per_locality_, {},
            add, {});
      }
    } else if (!add.empty() || !remove.empty()) {
      local_priority_set_.getOrCreateHostSet(0).updateHosts(
          local_hosts_, local_hosts_, local_hosts_per_locality_, local_hosts_per_locality_, {}, add,
          remove);
    }
  }

  void doLbTypeTest(LoadBalancerType type) {
    EXPECT_CALL(subset_info_, fallbackPolicy())
        .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

    lb_type_ = type;
    init({{"tcp://127.0.0.1:80", {{"version", "1.0"}}}});

    EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

    HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}});
    modifyHosts({added_host}, {host_set_.hosts_.back()});

    EXPECT_EQ(added_host, lb_->chooseHost(nullptr));
  }

  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  NiceMock<MockLoadBalancerSubsetInfo> subset_info_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::api::v2::Cluster::RingHashLbConfig ring_hash_lb_config_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  PrioritySetImpl local_priority_set_;
  HostVectorSharedPtr local_hosts_;
  HostsPerLocalitySharedPtr local_hosts_per_locality_;
  std::shared_ptr<SubsetLoadBalancer> lb_;
};

TEST_F(SubsetLoadBalancerTest, NoFallback) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

// Validate that SubsetLoadBalancer unregisters its priority set member update
// callback. Regression for heap-use-after-free.
TEST_F(SubsetLoadBalancerTest, DeregisterCallbacks) {
  init();
  lb_.reset();
  host_set_.runCallbacks({}, {});
}

TEST_P(SubsetLoadBalancerTest, NoFallbackAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}})}, {host_set_.hosts_.back()});

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackAnyEndpoint) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackAnyEndpointAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}});
  modifyHosts({added_host}, {host_set_.hosts_.back()});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(added_host, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackDefaultSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  init({
      {"tcp://127.0.0.1:80", {{"version", "new"}}},
      {"tcp://127.0.0.1:81", {{"version", "default"}}},
  });

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackDefaultSubsetAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  init({
      {"tcp://127.0.0.1:80", {{"version", "new"}}},
      {"tcp://127.0.0.1:81", {{"version", "default"}}},
  });

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(nullptr));

  HostSharedPtr added_host1 = makeHost("tcp://127.0.0.1:8000", {{"version", "new"}});
  HostSharedPtr added_host2 = makeHost("tcp://127.0.0.1:8001", {{"version", "default"}});

  modifyHosts({added_host1, added_host2}, {host_set_.hosts_.back()});

  EXPECT_EQ(added_host2, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackEmptyDefaultSubsetConvertsToAnyEndpoint) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  EXPECT_CALL(subset_info_, defaultSubset())
      .WillRepeatedly(ReturnRef(ProtobufWkt::Struct::default_instance()));

  init();

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_F(SubsetLoadBalancerTest, FallbackOnUnknownMetadata) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"version", "unknown"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_unknown_value));
}

TEST_F(SubsetLoadBalancerTest, BalancesSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_11({{"version", "1.1"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, BalancesSubsetAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_11({{"version", "1.1"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));
  EXPECT_EQ(2U, stats_.lb_subsets_created_.value());

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
               makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
              {host_set_.hosts_[1], host_set_.hosts_[2]});

  TestLoadBalancerContext context_12({{"version", "1.2"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_12));
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
}

// Test that adding backends to a failover group causes no problems.
TEST_P(SubsetLoadBalancerTest, UpdateFailover) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));
  TestLoadBalancerContext context_10({{"version", "1.0"}});

  // Start with an empty lb. Chosing a host should result in failure.
  init({});
  EXPECT_TRUE(nullptr == lb_->chooseHost(&context_10).get());

  // Add hosts to the group at priority 1.
  // These hosts should be selected as there are no healthy hosts with priority 0
  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
               makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
              {}, {}, 1);
  EXPECT_FALSE(nullptr == lb_->chooseHost(&context_10).get());

  // Finally update the priority 0 hosts. The LB should now select hosts.
  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
               makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
              {}, {}, 0);
  EXPECT_FALSE(nullptr == lb_->chooseHost(&context_10).get());
}

TEST_P(SubsetLoadBalancerTest, UpdateRemovingLastSubsetHost) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
  });

  HostSharedPtr host_v10 = host_set_.hosts_[0];
  HostSharedPtr host_v11 = host_set_.hosts_[1];

  TestLoadBalancerContext context({{"version", "1.0"}});
  EXPECT_EQ(host_v10, lb_->chooseHost(&context));
  EXPECT_EQ(1U, stats_.lb_subsets_selected_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_created_.value());

  modifyHosts({}, {host_v10});

  // fallback to any endpoint
  EXPECT_EQ(host_v11, lb_->chooseHost(&context));
  EXPECT_EQ(1U, stats_.lb_subsets_selected_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_removed_.value());
}

TEST_P(SubsetLoadBalancerTest, UpdateRemovingUnknownHost) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"stage", "version"}, {"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"stage", "prod"}, {"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"stage", "prod"}, {"version", "1.1"}}},
  });

  TestLoadBalancerContext context({{"stage", "prod"}, {"version", "1.0"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context));

  modifyHosts({}, {makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
                   makeHost("tcp://127.0.0.1:8001", {{"stage", "prod"}, {"version", "1.2"}})});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context));
}

TEST_F(SubsetLoadBalancerTest, UpdateModifyingOnlyHostHealth) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}, {"hardware"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_11({{"version", "1.1"}});

  // All hosts are healthy.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));

  host_set_.hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  host_set_.hosts_[2]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  host_set_.healthy_hosts_ = {host_set_.hosts_[1], host_set_.hosts_[3]};
  host_set_.runCallbacks({}, {});

  // Unhealthy hosts are excluded.
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));
}

TEST_F(SubsetLoadBalancerTest, BalancesDisjointSubsets) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}, {"hardware"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}, {"hardware", "std"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}, {"hardware", "bigmem"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.1"}, {"hardware", "std"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}, {"hardware", "bigmem"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_bigmem({{"hardware", "bigmem"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_bigmem));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_bigmem));
}

TEST_F(SubsetLoadBalancerTest, BalancesOverlappingSubsets) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {
      {"stage", "version"},
      {"version"},
  };
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.0"}, {"stage", "off"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:84", {{"version", "999"}, {"stage", "dev"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_10_prod({{"version", "1.0"}, {"stage", "prod"}});
  TestLoadBalancerContext context_dev({{"version", "999"}, {"stage", "dev"}});
  TestLoadBalancerContext context_unknown({{"version", "2.0"}, {"stage", "prod"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_10));

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10_prod));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10_prod));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10_prod));

  EXPECT_EQ(host_set_.hosts_[4], lb_->chooseHost(&context_dev));
  EXPECT_EQ(host_set_.hosts_[4], lb_->chooseHost(&context_dev));

  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown));
}

TEST_F(SubsetLoadBalancerTest, BalancesNestedSubsets) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {
      {"stage", "version"},
      {"stage"},
  };
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.0"}, {"stage", "off"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:84", {{"version", "999"}, {"stage", "dev"}}},
  });

  TestLoadBalancerContext context_prod({{"stage", "prod"}});
  TestLoadBalancerContext context_prod_10({{"version", "1.0"}, {"stage", "prod"}});
  TestLoadBalancerContext context_unknown_stage({{"stage", "larval"}});
  TestLoadBalancerContext context_unknown_version({{"version", "2.0"}, {"stage", "prod"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_prod));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_prod));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_prod));

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_prod_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_prod_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_prod_10));

  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_stage));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_version));
}

TEST_F(SubsetLoadBalancerTest, IgnoresUnselectedMetadata) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}, {"stage", "ignored"}}},
      {"tcp://127.0.0.1:81", {{"ignore", "value"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
  });

  TestLoadBalancerContext context_ignore({{"ignore", "value"}});
  TestLoadBalancerContext context_version({{"version", "1.0"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_version));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_version));

  EXPECT_EQ(nullptr, lb_->chooseHost(&context_ignore));
}

TEST_F(SubsetLoadBalancerTest, IgnoresHostsWithoutMetadata) {
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  HostVector hosts;
  hosts.emplace_back(makeTestHost(info_, "tcp://127.0.0.1:80"));
  hosts.emplace_back(makeHost("tcp://127.0.0.1:81", {{"version", "1.0"}}));

  host_set_.hosts_ = hosts;
  host_set_.hosts_per_locality_ = makeHostsPerLocality({hosts});

  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;

  lb_.reset(new SubsetLoadBalancer(lb_type_, priority_set_, nullptr, stats_, runtime_, random_,
                                   subset_info_, ring_hash_lb_config_, common_config_));

  TestLoadBalancerContext context_version({{"version", "1.0"}});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_version));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_version));
}

// TODO(mattklein123): The following 4 tests verify basic functionality with all sub-LB tests.
// Optimally these would also be some type of TEST_P, but that is a little bit complicated as
// modifyHosts() also needs params. Clean this up.
TEST_P(SubsetLoadBalancerTest, LoadBalancerTypesRoundRobin) {
  doLbTypeTest(LoadBalancerType::RoundRobin);
}

TEST_P(SubsetLoadBalancerTest, LoadBalancerTypesLeastRequest) {
  doLbTypeTest(LoadBalancerType::LeastRequest);
}

TEST_P(SubsetLoadBalancerTest, LoadBalancerTypesRandom) { doLbTypeTest(LoadBalancerType::Random); }

TEST_P(SubsetLoadBalancerTest, LoadBalancerTypesRingHash) {
  doLbTypeTest(LoadBalancerType::RingHash);
}

TEST_P(SubsetLoadBalancerTest, LoadBalancerTypesMaglev) { doLbTypeTest(LoadBalancerType::Maglev); }

TEST_F(SubsetLoadBalancerTest, ZoneAwareFallback) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<std::set<std::string>> subset_keys = {{"x"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  common_config_.mutable_healthy_panic_threshold()->set_value(40);
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 40))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
                 },
                 {
                     {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:83", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:84", {{"version", "1.1"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "1.0"}}},
                 },
                 {
                     {"tcp://127.0.0.1:91", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "1.0"}}},
                 }});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
}

TEST_P(SubsetLoadBalancerTest, ZoneAwareFallbackAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<std::set<std::string>> subset_keys = {{"x"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
                 },
                 {
                     {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:83", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:84", {{"version", "1.1"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "1.0"}}},
                 },
                 {
                     {"tcp://127.0.0.1:91", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "1.0"}}},
                 }});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}})}, {host_set_.hosts_[0]},
              absl::optional<uint32_t>(0));

  modifyLocalHosts({makeHost("tcp://127.0.0.1:9000", {{"version", "1.0"}})}, {local_hosts_->at(0)},
                   0);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, ZoneAwareFallbackDefaultSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "new"}}},
                     {"tcp://127.0.0.1:81", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:82", {{"version", "new"}}},
                     {"tcp://127.0.0.1:83", {{"version", "default"}}},
                     {"tcp://127.0.0.1:84", {{"version", "new"}}},
                     {"tcp://127.0.0.1:85", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:86", {{"version", "new"}}},
                     {"tcp://127.0.0.1:87", {{"version", "default"}}},
                     {"tcp://127.0.0.1:88", {{"version", "new"}}},
                     {"tcp://127.0.0.1:89", {{"version", "default"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "new"}}},
                     {"tcp://127.0.0.1:91", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "new"}}},
                     {"tcp://127.0.0.1:93", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:94", {{"version", "new"}}},
                     {"tcp://127.0.0.1:95", {{"version", "default"}}},
                 }});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(nullptr));
}

TEST_P(SubsetLoadBalancerTest, ZoneAwareFallbackDefaultSubsetAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "new"}}},
                     {"tcp://127.0.0.1:81", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:82", {{"version", "new"}}},
                     {"tcp://127.0.0.1:83", {{"version", "default"}}},
                     {"tcp://127.0.0.1:84", {{"version", "new"}}},
                     {"tcp://127.0.0.1:85", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:86", {{"version", "new"}}},
                     {"tcp://127.0.0.1:87", {{"version", "default"}}},
                     {"tcp://127.0.0.1:88", {{"version", "new"}}},
                     {"tcp://127.0.0.1:89", {{"version", "default"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "new"}}},
                     {"tcp://127.0.0.1:91", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "new"}}},
                     {"tcp://127.0.0.1:93", {{"version", "default"}}},
                 },
                 {
                     {"tcp://127.0.0.1:94", {{"version", "new"}}},
                     {"tcp://127.0.0.1:95", {{"version", "default"}}},
                 }});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8001", {{"version", "default"}})}, {host_set_.hosts_[1]},
              absl::optional<uint32_t>(0));

  modifyLocalHosts({local_hosts_->at(1)},
                   {makeHost("tcp://127.0.0.1:9001", {{"version", "default"}})}, 0);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, ZoneAwareBalancesSubsets) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
                     {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:86", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:87", {{"version", "1.1"}}},
                     {"tcp://127.0.0.1:88", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:89", {{"version", "1.1"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:91", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:93", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:94", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:95", {{"version", "1.1"}}},
                 }});

  TestLoadBalancerContext context({{"version", "1.1"}});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(&context));
}

TEST_P(SubsetLoadBalancerTest, ZoneAwareBalancesSubsetsAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  zoneAwareInit({{
                     {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
                     {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:86", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:87", {{"version", "1.1"}}},
                     {"tcp://127.0.0.1:88", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:89", {{"version", "1.1"}}},
                 }},
                {{
                     {"tcp://127.0.0.1:90", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:91", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:92", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:93", {{"version", "1.1"}}},
                 },
                 {
                     {"tcp://127.0.0.1:94", {{"version", "1.0"}}},
                     {"tcp://127.0.0.1:95", {{"version", "1.1"}}},
                 }});

  TestLoadBalancerContext context({{"version", "1.1"}});

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(&context));

  modifyHosts({makeHost("tcp://127.0.0.1:8001", {{"version", "1.1"}})}, {host_set_.hosts_[1]},
              absl::optional<uint32_t>(0));

  modifyLocalHosts({local_hosts_->at(1)}, {makeHost("tcp://127.0.0.1:9001", {{"version", "1.1"}})},
                   0);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(&context));
}

TEST_F(SubsetLoadBalancerTest, DescribeMetadata) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));
  init();

  ProtobufWkt::Value str_value;
  str_value.set_string_value("abc");

  ProtobufWkt::Value num_value;
  num_value.set_number_value(100);

  auto tester = SubsetLoadBalancerDescribeMetadataTester(lb_);
  tester.test("version=\"abc\"", {{"version", str_value}});
  tester.test("number=100", {{"number", num_value}});
  tester.test("x=\"abc\", y=100", {{"x", str_value}, {"y", num_value}});
  tester.test("y=100, x=\"abc\"", {{"y", num_value}, {"x", str_value}});
  tester.test("<no metadata>", {});
}

INSTANTIATE_TEST_CASE_P(UpdateOrderings, SubsetLoadBalancerTest,
                        testing::ValuesIn({REMOVES_FIRST, SIMULTANEOUS}));

} // namespace SubsetLoadBalancerTest
} // namespace Upstream
} // namespace Envoy
