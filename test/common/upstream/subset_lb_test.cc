#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optional.h"

#include "common/config/metadata.h"
#include "common/upstream/subset_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "api/cds.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
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
  Optional<uint64_t> computeHashKey() override { return {}; }
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

  void init(const HostURLMetadataMap& host_metadata) {
    EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

    std::vector<HostSharedPtr> hosts;
    for (const auto& it : host_metadata) {
      hosts.emplace_back(makeHost(it.first, it.second));
    }

    cluster_.hosts_ = hosts;
    cluster_.hosts_per_locality_ = std::vector<std::vector<HostSharedPtr>>({hosts});

    cluster_.healthy_hosts_ = cluster_.hosts_;
    cluster_.healthy_hosts_per_locality_ = cluster_.hosts_per_locality_;

    lb_.reset(new SubsetLoadBalancer(lb_type_, cluster_, nullptr, stats_, runtime_, random_,
                                     subset_info_, ring_hash_lb_config_));
  }

  void zoneAwareInit(const std::vector<HostURLMetadataMap>& host_metadata_per_locality,
                     const std::vector<HostURLMetadataMap>& local_host_metadata_per_locality) {
    EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

    std::vector<HostSharedPtr> hosts;
    std::vector<std::vector<HostSharedPtr>> hosts_per_locality;
    for (const auto& host_metadata : host_metadata_per_locality) {
      std::vector<HostSharedPtr> locality_hosts;
      for (const auto& host_entry : host_metadata) {
        HostSharedPtr host = makeHost(host_entry.first, host_entry.second);
        hosts.emplace_back(host);
        locality_hosts.emplace_back(host);
      }
      hosts_per_locality.emplace_back(locality_hosts);
    }

    cluster_.hosts_ = hosts;
    cluster_.hosts_per_locality_ = hosts_per_locality;

    cluster_.healthy_hosts_ = cluster_.healthy_hosts_;
    cluster_.healthy_hosts_per_locality_ = cluster_.hosts_per_locality_;

    local_hosts_.reset(new std::vector<HostSharedPtr>());
    local_hosts_per_locality_.reset(new std::vector<std::vector<HostSharedPtr>>());
    for (const auto& local_host_metadata : local_host_metadata_per_locality) {
      std::vector<HostSharedPtr> local_locality_hosts;
      for (const auto& host_entry : local_host_metadata) {
        HostSharedPtr host = makeHost(host_entry.first, host_entry.second);
        local_hosts_->emplace_back(host);
        local_locality_hosts.emplace_back(host);
      }
      local_hosts_per_locality_->emplace_back(local_locality_hosts);
    }

    local_host_set_.reset(new HostSetImpl());
    local_host_set_->updateHosts(local_hosts_, local_hosts_, local_hosts_per_locality_,
                                 local_hosts_per_locality_, {}, {});

    lb_.reset(new SubsetLoadBalancer(lb_type_, cluster_, local_host_set_.get(), stats_, runtime_,
                                     random_, subset_info_, ring_hash_lb_config_));
  }

  HostSharedPtr makeHost(const std::string& url, const HostMetadata& metadata) {
    envoy::api::v2::Metadata m;
    for (const auto& m_it : metadata) {
      Config::Metadata::mutableMetadataValue(m, Config::MetadataFilters::get().ENVOY_LB, m_it.first)
          .set_string_value(m_it.second);
    }

    return makeTestHost(cluster_.info_, url, m);
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

  void modifyHosts(std::vector<HostSharedPtr> add, std::vector<HostSharedPtr> remove,
                   Optional<uint32_t> add_in_locality = {}) {
    for (const auto& host : remove) {
      auto it = std::find(cluster_.hosts_.begin(), cluster_.hosts_.end(), host);
      if (it != cluster_.hosts_.end()) {
        cluster_.hosts_.erase(it);
      }
      cluster_.healthy_hosts_ = cluster_.hosts_;

      for (auto& locality_hosts : cluster_.hosts_per_locality_) {
        auto it = std::find(locality_hosts.begin(), locality_hosts.end(), host);
        if (it != locality_hosts.end()) {
          locality_hosts.erase(it);
        }
      }
      cluster_.healthy_hosts_per_locality_ = cluster_.hosts_per_locality_;
    }

    if (GetParam() == REMOVES_FIRST && !remove.empty()) {
      cluster_.runCallbacks({}, remove);
    }

    for (const auto& host : add) {
      cluster_.hosts_.emplace_back(host);
      cluster_.healthy_hosts_ = cluster_.hosts_;

      if (add_in_locality.valid()) {
        cluster_.hosts_per_locality_[add_in_locality.value()].emplace_back(host);
        cluster_.healthy_hosts_per_locality_ = cluster_.hosts_per_locality_;
      }
    }

    if (GetParam() == REMOVES_FIRST) {
      if (!add.empty()) {
        cluster_.runCallbacks(add, {});
      }
    } else if (!add.empty() || !remove.empty()) {
      cluster_.runCallbacks(add, remove);
    }
  }

  void modifyLocalHosts(std::vector<HostSharedPtr> add, std::vector<HostSharedPtr> remove,
                        uint32_t add_in_locality) {
    for (const auto& host : remove) {
      auto it = std::find(local_hosts_->begin(), local_hosts_->end(), host);
      if (it != local_hosts_->end()) {
        local_hosts_->erase(it);
      }

      for (auto& locality_hosts : *local_hosts_per_locality_) {
        auto it = std::find(locality_hosts.begin(), locality_hosts.end(), host);
        if (it != locality_hosts.end()) {
          locality_hosts.erase(it);
        }
      }
    }

    if (GetParam() == REMOVES_FIRST && !remove.empty()) {
      local_host_set_->updateHosts(local_hosts_, local_hosts_, local_hosts_per_locality_,
                                   local_hosts_per_locality_, {}, remove);
    }

    for (const auto& host : add) {
      local_hosts_->emplace_back(host);
      (*local_hosts_per_locality_)[add_in_locality].emplace_back(host);
    }

    if (GetParam() == REMOVES_FIRST) {
      if (!add.empty()) {
        local_host_set_->updateHosts(local_hosts_, local_hosts_, local_hosts_per_locality_,
                                     local_hosts_per_locality_, add, {});
      }
    } else if (!add.empty() || !remove.empty()) {
      local_host_set_->updateHosts(local_hosts_, local_hosts_, local_hosts_per_locality_,
                                   local_hosts_per_locality_, add, remove);
    }
  }

  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  NiceMock<MockCluster> cluster_;
  NiceMock<MockLoadBalancerSubsetInfo> subset_info_;
  envoy::api::v2::Cluster::RingHashLbConfig ring_hash_lb_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  std::shared_ptr<HostSetImpl> local_host_set_;
  HostVectorSharedPtr local_hosts_;
  HostListsSharedPtr local_hosts_per_locality_;
  std::shared_ptr<LoadBalancer> lb_;
};

TEST_F(SubsetLoadBalancerTest, NoFallback) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, NoFallbackAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}})}, {cluster_.hosts_.back()});

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackAnyEndpoint) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackAnyEndpointAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(nullptr));

  HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}});
  modifyHosts({added_host}, {cluster_.hosts_.back()});

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

  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(nullptr));
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

  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(nullptr));

  HostSharedPtr added_host1 = makeHost("tcp://127.0.0.1:8000", {{"version", "new"}});
  HostSharedPtr added_host2 = makeHost("tcp://127.0.0.1:8001", {{"version", "default"}});

  modifyHosts({added_host1, added_host2}, {cluster_.hosts_.back()});

  EXPECT_EQ(added_host2, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackEmptyDefaultSubsetConvertsToAnyEndpoint) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  EXPECT_CALL(subset_info_, defaultSubset())
      .WillRepeatedly(ReturnRef(ProtobufWkt::Struct::default_instance()));

  init();

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_F(SubsetLoadBalancerTest, FallbackOnUnknownMetadata) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"version", "unknown"}});

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_unknown_value));
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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[3], lb_->chooseHost(&context_11));
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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(cluster_.hosts_[3], lb_->chooseHost(&context_11));
  EXPECT_EQ(2U, stats_.lb_subsets_created_.value());

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
               makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
              {cluster_.hosts_[1], cluster_.hosts_[2]});

  TestLoadBalancerContext context_12({{"version", "1.2"}});

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[3], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_11));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(&context_12));
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
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

  HostSharedPtr host_v10 = cluster_.hosts_[0];
  HostSharedPtr host_v11 = cluster_.hosts_[1];

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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context));

  modifyHosts({}, {makeHost("tcp://127.0.0.1:8000", {{"version", "1.2"}}),
                   makeHost("tcp://127.0.0.1:8001", {{"stage", "prod"}, {"version", "1.2"}})});

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context));
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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_bigmem));
  EXPECT_EQ(cluster_.hosts_[3], lb_->chooseHost(&context_bigmem));
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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(&context_10));

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10_prod));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_10_prod));
  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_10_prod));

  EXPECT_EQ(cluster_.hosts_[4], lb_->chooseHost(&context_dev));
  EXPECT_EQ(cluster_.hosts_[4], lb_->chooseHost(&context_dev));

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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_prod));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_prod));
  EXPECT_EQ(cluster_.hosts_[3], lb_->chooseHost(&context_prod));

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_prod_10));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_prod_10));
  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_prod_10));

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

  EXPECT_EQ(cluster_.hosts_[0], lb_->chooseHost(&context_version));
  EXPECT_EQ(cluster_.hosts_[2], lb_->chooseHost(&context_version));

  EXPECT_EQ(nullptr, lb_->chooseHost(&context_ignore));
}

TEST_F(SubsetLoadBalancerTest, IgnoresHostsWithoutMetadata) {
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<std::set<std::string>> subset_keys = {{"version"}};
  EXPECT_CALL(subset_info_, subsetKeys()).WillRepeatedly(ReturnRef(subset_keys));

  std::vector<HostSharedPtr> hosts;
  hosts.emplace_back(makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"));
  hosts.emplace_back(makeHost("tcp://127.0.0.1:81", {{"version", "1.0"}}));

  cluster_.hosts_ = hosts;
  cluster_.hosts_per_locality_ = std::vector<std::vector<HostSharedPtr>>({hosts});

  cluster_.healthy_hosts_ = cluster_.hosts_;
  cluster_.healthy_hosts_per_locality_ = cluster_.hosts_per_locality_;

  lb_.reset(new SubsetLoadBalancer(lb_type_, cluster_, nullptr, stats_, runtime_, random_,
                                   subset_info_, ring_hash_lb_config_));

  TestLoadBalancerContext context_version({{"version", "1.0"}});

  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_version));
  EXPECT_EQ(cluster_.hosts_[1], lb_->chooseHost(&context_version));
}

TEST_F(SubsetLoadBalancerTest, LoadBalancerTypes) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::api::v2::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  auto types =
      std::vector<LoadBalancerType>({LoadBalancerType::RoundRobin, LoadBalancerType::LeastRequest,
                                     LoadBalancerType::Random, LoadBalancerType::RingHash});

  for (const auto& it : types) {
    lb_type_ = it;
    init();

    EXPECT_NE(nullptr, lb_->chooseHost(nullptr));
  }
}

TEST_F(SubsetLoadBalancerTest, ZoneAwareFallback) {
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][1], lb_->chooseHost(nullptr));
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][1], lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}})}, {cluster_.hosts_[0]},
              Optional<uint32_t>(0));

  modifyLocalHosts({makeHost("tcp://127.0.0.1:9000", {{"version", "1.0"}})}, {local_hosts_->at(0)},
                   0);

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][0], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][1], lb_->chooseHost(nullptr));
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(nullptr));
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8001", {{"version", "default"}})}, {cluster_.hosts_[1]},
              Optional<uint32_t>(0));

  modifyLocalHosts({local_hosts_->at(1)},
                   {makeHost("tcp://127.0.0.1:9001", {{"version", "default"}})}, 0);

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(nullptr));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(nullptr));
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(&context));
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

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(&context));

  modifyHosts({makeHost("tcp://127.0.0.1:8001", {{"version", "1.1"}})}, {cluster_.hosts_[1]},
              Optional<uint32_t>(0));

  modifyLocalHosts({local_hosts_->at(1)}, {makeHost("tcp://127.0.0.1:9001", {{"version", "1.1"}})},
                   0);

  EXPECT_CALL(random_, random()).WillOnce(Return(100));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[0][1], lb_->chooseHost(&context));

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(cluster_.healthy_hosts_per_locality_[1][3], lb_->chooseHost(&context));
}

INSTANTIATE_TEST_CASE_P(UpdateOrderings, SubsetLoadBalancerTest,
                        testing::ValuesIn({REMOVES_FIRST, SIMULTANEOUS}));

} // namespace SubsetLoadBalancerTest
} // namespace Upstream
} // namespace Envoy
