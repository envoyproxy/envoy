#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/upstream/subset_lb.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class SubsetLoadBalancerDescribeMetadataTester {
public:
  SubsetLoadBalancerDescribeMetadataTester(std::shared_ptr<SubsetLoadBalancer> lb) : lb_(lb) {}

  using MetadataVector = std::vector<std::pair<std::string, ProtobufWkt::Value>>;

  void test(std::string expected, const MetadataVector& metadata) {
    const SubsetLoadBalancer::SubsetMetadata& subset_metadata(metadata);
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

  TestMetadataMatchCriteria(const std::map<std::string, ProtobufWkt::Value> matches) {
    for (const auto& it : matches) {
      matches_.emplace_back(
          std::make_shared<const TestMetadataMatchCriterion>(it.first, HashedValue(it.second)));
    }
  }

  const std::vector<Router::MetadataMatchCriterionConstSharedPtr>&
  metadataMatchCriteria() const override {
    return matches_;
  }

  Router::MetadataMatchCriteriaConstPtr
  mergeMatchCriteria(const ProtobufWkt::Struct& override) const override {
    auto new_criteria = std::make_unique<TestMetadataMatchCriteria>(*this);

    // TODO: this is copied from MetadataMatchCriteriaImpl::extractMetadataMatchCriteria.
    //   should we start using real impl?
    std::vector<Router::MetadataMatchCriterionConstSharedPtr> v;
    absl::node_hash_map<std::string, std::size_t> existing;

    for (const auto& it : matches_) {
      existing.emplace(it->name(), v.size());
      v.emplace_back(it);
    }

    // Add values from matches, replacing name/values copied from parent.
    for (const auto& it : override.fields()) {
      const auto index_it = existing.find(it.first);
      if (index_it != existing.end()) {
        v[index_it->second] = std::make_shared<TestMetadataMatchCriterion>(it.first, it.second);
      } else {
        v.emplace_back(std::make_shared<TestMetadataMatchCriterion>(it.first, it.second));
      }
    }
    std::sort(v.begin(), v.end(),
              [](const Router::MetadataMatchCriterionConstSharedPtr& a,
                 const Router::MetadataMatchCriterionConstSharedPtr& b) -> bool {
                return a->name() < b->name();
              });

    new_criteria->matches_ = v;
    return new_criteria;
  }

  Router::MetadataMatchCriteriaConstPtr
  filterMatchCriteria(const std::set<std::string>& names) const override {
    auto new_criteria = std::make_unique<TestMetadataMatchCriteria>(*this);
    for (auto it = new_criteria->matches_.begin(); it != new_criteria->matches_.end();) {
      if (names.count(it->get()->name()) == 0) {
        it = new_criteria->matches_.erase(it);
      } else {
        it++;
      }
    }
    return new_criteria;
  }

private:
  std::vector<Router::MetadataMatchCriterionConstSharedPtr> matches_;
};

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(
      std::initializer_list<std::map<std::string, std::string>::value_type> metadata_matches)
      : matches_(
            new TestMetadataMatchCriteria(std::map<std::string, std::string>(metadata_matches))) {}

  TestLoadBalancerContext(
      std::initializer_list<std::map<std::string, ProtobufWkt::Value>::value_type> metadata_matches)
      : matches_(new TestMetadataMatchCriteria(
            std::map<std::string, ProtobufWkt::Value>(metadata_matches))) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return {}; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override { return matches_.get(); }
  const Http::RequestHeaderMap* downstreamHeaders() const override { return nullptr; }

private:
  const std::shared_ptr<Router::MetadataMatchCriteria> matches_;
};

enum class UpdateOrder { RemovesFirst, Simultaneous };

class SubsetLoadBalancerTest : public Event::TestUsingSimulatedTime,
                               public testing::TestWithParam<UpdateOrder> {
public:
  SubsetLoadBalancerTest()
      : scope_(stats_store_.createScope("testprefix")), stat_names_(stats_store_.symbolTable()),
        stats_(stat_names_, stats_store_) {
    least_request_lb_config_.mutable_choice_count()->set_value(2);
  }

  using HostMetadata = std::map<std::string, std::string>;
  using HostListMetadata = std::map<std::string, std::vector<std::string>>;
  using HostURLMetadataMap = std::map<std::string, HostMetadata>;

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

    host_set.runCallbacks({}, {});
  }

  void configureWeightedHostSet(const HostURLMetadataMap& first_locality_host_metadata,
                                const HostURLMetadataMap& second_locality_host_metadata,
                                MockHostSet& host_set, LocalityWeights locality_weights) {
    HostVector first_locality;
    HostVector all_hosts;
    for (const auto& it : first_locality_host_metadata) {
      auto host = makeHost(it.first, it.second);
      first_locality.emplace_back(host);
      all_hosts.emplace_back(host);
    }

    HostVector second_locality;
    for (const auto& it : second_locality_host_metadata) {
      auto host = makeHost(it.first, it.second);
      second_locality.emplace_back(host);
      all_hosts.emplace_back(host);
    }

    host_set.hosts_ = all_hosts;
    host_set.hosts_per_locality_ = makeHostsPerLocality({first_locality, second_locality});
    host_set.healthy_hosts_ = host_set.hosts_;
    host_set.healthy_hosts_per_locality_ = host_set.hosts_per_locality_;
    host_set.locality_weights_ = std::make_shared<const LocalityWeights>(locality_weights);
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

    lb_ = std::make_shared<SubsetLoadBalancer>(
        lb_type_, priority_set_, nullptr, stats_, *scope_, runtime_, random_, subset_info_,
        ring_hash_lb_config_, maglev_lb_config_, round_robin_lb_config_, least_request_lb_config_,
        common_config_, simTime());
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

    host_set_.healthy_hosts_ = host_set_.hosts_;
    host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;

    local_hosts_ = std::make_shared<HostVector>();
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

    local_priority_set_.updateHosts(
        0,
        HostSetImpl::updateHostsParams(
            local_hosts_, local_hosts_per_locality_,
            std::make_shared<HealthyHostVector>(*local_hosts_), local_hosts_per_locality_,
            std::make_shared<DegradedHostVector>(), HostsPerLocalityImpl::empty(),
            std::make_shared<ExcludedHostVector>(), HostsPerLocalityImpl::empty()),
        {}, {}, {}, absl::nullopt);

    lb_ = std::make_shared<SubsetLoadBalancer>(
        lb_type_, priority_set_, &local_priority_set_, stats_, *scope_, runtime_, random_,
        subset_info_, ring_hash_lb_config_, maglev_lb_config_, round_robin_lb_config_,
        least_request_lb_config_, common_config_, simTime());
  }

  HostSharedPtr makeHost(const std::string& url, const HostMetadata& metadata) {
    envoy::config::core::v3::Metadata m;
    for (const auto& m_it : metadata) {
      Config::Metadata::mutableMetadataValue(m, Config::MetadataFilters::get().ENVOY_LB, m_it.first)
          .set_string_value(m_it.second);
    }

    return makeTestHost(info_, url, m, simTime());
  }
  HostSharedPtr makeHost(const std::string& url, const HostListMetadata& metadata) {
    envoy::config::core::v3::Metadata m;
    for (const auto& m_it : metadata) {
      auto& metadata = Config::Metadata::mutableMetadataValue(
          m, Config::MetadataFilters::get().ENVOY_LB, m_it.first);
      for (const auto& value : m_it.second) {
        metadata.mutable_list_value()->add_values()->set_string_value(value);
      }
    }

    return makeTestHost(info_, url, m, simTime());
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

  SubsetSelectorPtr
  makeSelector(const std::set<std::string>& selector_keys,
               envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
                   LbSubsetSelectorFallbackPolicy fallback_policy,
               const std::set<std::string>& fallback_keys_subset,
               bool single_host_per_subset = false) {
    Protobuf::RepeatedPtrField<std::string> selector_keys_mapped;
    for (const auto& it : selector_keys) {
      selector_keys_mapped.Add(std::string(it));
    }

    Protobuf::RepeatedPtrField<std::string> fallback_keys_subset_mapped;
    for (const auto& it : fallback_keys_subset) {
      fallback_keys_subset_mapped.Add(std::string(it));
    }

    return std::make_shared<SubsetSelectorImpl>(
        selector_keys_mapped, fallback_policy, fallback_keys_subset_mapped, single_host_per_subset);
  }

  SubsetSelectorPtr makeSelector(
      const std::set<std::string>& selector_keys,
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::
          LbSubsetSelectorFallbackPolicy fallback_policy =
              envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED) {
    return makeSelector(selector_keys, fallback_policy, {});
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

    if (GetParam() == UpdateOrder::RemovesFirst && !remove.empty()) {
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

    if (GetParam() == UpdateOrder::RemovesFirst) {
      if (!add.empty()) {
        host_set.runCallbacks(add, {});
      }
    } else if (!add.empty() || !remove.empty()) {
      host_set.runCallbacks(add, remove);
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

    if (GetParam() == UpdateOrder::RemovesFirst && !remove.empty()) {
      local_priority_set_.updateHosts(
          0,
          updateHostsParams(local_hosts_, local_hosts_per_locality_,
                            std::make_shared<HealthyHostVector>(*local_hosts_),
                            local_hosts_per_locality_),
          {}, {}, remove, absl::nullopt);
    }

    for (const auto& host : add) {
      local_hosts_->emplace_back(host);
      std::vector<HostVector> locality_hosts_copy = local_hosts_per_locality_->get();
      locality_hosts_copy[add_in_locality].emplace_back(host);
      local_hosts_per_locality_ = makeHostsPerLocality(std::move(locality_hosts_copy));
    }

    if (GetParam() == UpdateOrder::RemovesFirst) {
      if (!add.empty()) {
        local_priority_set_.updateHosts(
            0,
            updateHostsParams(local_hosts_, local_hosts_per_locality_,
                              std::make_shared<HealthyHostVector>(*local_hosts_),
                              local_hosts_per_locality_),
            {}, add, {}, absl::nullopt);
      }
    } else if (!add.empty() || !remove.empty()) {
      local_priority_set_.updateHosts(
          0,
          updateHostsParams(local_hosts_, local_hosts_per_locality_,
                            std::make_shared<const HealthyHostVector>(*local_hosts_),
                            local_hosts_per_locality_),
          {}, add, remove, absl::nullopt);
    }
  }

  void doLbTypeTest(LoadBalancerType type) {
    EXPECT_CALL(subset_info_, fallbackPolicy())
        .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

    lb_type_ = type;
    init({{"tcp://127.0.0.1:80", {{"version", "1.0"}}}});

    EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

    HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}});
    modifyHosts({added_host}, {host_set_.hosts_.back()});

    EXPECT_EQ(added_host, lb_->chooseHost(nullptr));
  }

  MetadataConstSharedPtr buildMetadata(const std::string& version, bool is_default = false) const {
    envoy::config::core::v3::Metadata metadata;

    if (!version.empty()) {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "version")
          .set_string_value(version);
    }

    if (is_default) {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "default")
          .set_string_value("true");
    }

    return std::make_shared<const envoy::config::core::v3::Metadata>(metadata);
  }

  MetadataConstSharedPtr buildMetadataWithStage(const std::string& version,
                                                const std::string& stage = "") const {
    envoy::config::core::v3::Metadata metadata;

    if (!version.empty()) {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "version")
          .set_string_value(version);
    }

    if (!stage.empty()) {
      Envoy::Config::Metadata::mutableMetadataValue(
          metadata, Config::MetadataFilters::get().ENVOY_LB, "stage")
          .set_string_value(stage);
    }

    return std::make_shared<const envoy::config::core::v3::Metadata>(metadata);
  }

  ProtobufWkt::Value valueFromJson(std::string json) {
    ProtobufWkt::Value v;
    TestUtility::loadFromJson(json, v);
    return v;
  }

  LoadBalancerType lb_type_{LoadBalancerType::RoundRobin};
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  NiceMock<MockLoadBalancerSubsetInfo> subset_info_;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::config::cluster::v3::Cluster::RingHashLbConfig ring_hash_lb_config_;
  envoy::config::cluster::v3::Cluster::MaglevLbConfig maglev_lb_config_;
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config_;
  envoy::config::cluster::v3::Cluster::RoundRobinLbConfig round_robin_lb_config_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr scope_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  PrioritySetImpl local_priority_set_;
  HostVectorSharedPtr local_hosts_;
  HostsPerLocalitySharedPtr local_hosts_per_locality_;
  std::shared_ptr<SubsetLoadBalancer> lb_;
};

TEST_F(SubsetLoadBalancerTest, NoFallback) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());

  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_FALSE(lb_->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<MockHost>>();
  EXPECT_FALSE(lb_->selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  init();

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  modifyHosts({makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}})}, {host_set_.hosts_.back()});

  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackAnyEndpoint) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackAnyEndpointAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"version", "1.0"}});
  modifyHosts({added_host}, {host_set_.hosts_.back()});

  EXPECT_EQ(added_host, lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, FallbackDefaultSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

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

TEST_F(SubsetLoadBalancerTest, FallbackPanicMode) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));
  EXPECT_CALL(subset_info_, panicModeAny()).WillRepeatedly(Return(true));

  // The default subset will be empty.
  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "none"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  init({
      {"tcp://127.0.0.1:80", {{"version", "new"}}},
      {"tcp://127.0.0.1:81", {{"version", "default"}}},
  });

  EXPECT_TRUE(lb_->chooseHost(nullptr) != nullptr);
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_panic_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackPanicModeWithUpdates) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));
  EXPECT_CALL(subset_info_, panicModeAny()).WillRepeatedly(Return(true));

  // The default subset will be empty.
  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "none"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  init({{"tcp://127.0.0.1:80", {{"version", "default"}}}});
  EXPECT_TRUE(lb_->chooseHost(nullptr) != nullptr);

  // Removing current host, adding a new one.
  HostSharedPtr added_host = makeHost("tcp://127.0.0.2:8000", {{"version", "new"}});
  modifyHosts({added_host}, {host_set_.hosts_[0]});

  EXPECT_EQ(1, host_set_.hosts_.size());
  EXPECT_EQ(added_host, lb_->chooseHost(nullptr));
}

TEST_P(SubsetLoadBalancerTest, FallbackDefaultSubsetAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"version", "unknown"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_unknown_value));
}

TEST_F(SubsetLoadBalancerTest, BalancesSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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

TEST_P(SubsetLoadBalancerTest, ListAsAnyEnabled) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, listAsAny()).WillRepeatedly(Return(true));

  init({});
  modifyHosts(
      {makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.2.1", "1.2"}}}),
       makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
      {}, {}, 0);

  {
    TestLoadBalancerContext context({{"version", "1.0"}});
    EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context({{"version", "1.2"}});
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
  }
  TestLoadBalancerContext context({{"version", "1.2.1"}});
  EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
}

TEST_P(SubsetLoadBalancerTest, ListAsAnyEnabledMultipleLists) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, listAsAny()).WillRepeatedly(Return(true));

  init({});
  modifyHosts(
      {makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.2.1", "1.2"}}}),
       makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.2.2", "1.2"}}}),
       makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
      {}, {}, 0);

  {
    TestLoadBalancerContext context({{"version", "1.0"}});
    EXPECT_TRUE(host_set_.hosts()[2] == lb_->chooseHost(&context));
    EXPECT_TRUE(host_set_.hosts()[2] == lb_->chooseHost(&context));
  }
  {
    // This should LB between both hosts marked with version 1.2.
    TestLoadBalancerContext context({{"version", "1.2"}});
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
    EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
  }
  {
    // Choose a host multiple times to ensure that hosts()[0] is the *only*
    // thing selected for this subset.
    TestLoadBalancerContext context({{"version", "1.2.1"}});
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
  }

  TestLoadBalancerContext context({{"version", "1.2.2"}});
  EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
}

TEST_P(SubsetLoadBalancerTest, ListAsAnyEnabledMultipleListsForSingleHost) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version", "hardware"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, listAsAny()).WillRepeatedly(Return(true));

  init({});
  modifyHosts(
      {makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.2.1", "1.2"}},
                                         {"hardware", std::vector<std::string>{"a", "b"}}}),
       makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.1", "1.1.1"}},
                                         {"hardware", std::vector<std::string>{"b", "c"}}})},
      {}, {}, 0);

  {
    TestLoadBalancerContext context({{"version", "1.2"}, {"hardware", "a"}});
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
    EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
  }

  {
    TestLoadBalancerContext context({{"version", "1.1"}, {"hardware", "b"}});
    EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
    EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
  }

  {
    TestLoadBalancerContext context({{"version", "1.1"}, {"hardware", "a"}});
    EXPECT_TRUE(nullptr == lb_->chooseHost(&context));
  }

  TestLoadBalancerContext context({{"version", "1.2.1"}, {"hardware", "b"}});
  EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
  EXPECT_TRUE(host_set_.hosts()[0] == lb_->chooseHost(&context));
}

TEST_P(SubsetLoadBalancerTest, ListAsAnyDisable) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({});
  modifyHosts(
      {makeHost("tcp://127.0.0.1:8000", {{"version", std::vector<std::string>{"1.2.1", "1.2"}}}),
       makeHost("tcp://127.0.0.1:8001", {{"version", "1.0"}})},
      {}, {}, 0);

  {
    TestLoadBalancerContext context({{"version", "1.0"}});
    EXPECT_TRUE(host_set_.hosts()[1] == lb_->chooseHost(&context));
  }
  TestLoadBalancerContext context({{"version", "1.2"}});
  EXPECT_TRUE(nullptr == lb_->chooseHost(&context));
}

// Test that adding backends to a failover group causes no problems.
TEST_P(SubsetLoadBalancerTest, UpdateFailover) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  TestLoadBalancerContext context_10({{"version", "1.0"}});

  // Start with an empty lb. Choosing a host should result in failure.
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

TEST_P(SubsetLoadBalancerTest, OnlyMetadataChanged) {
  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_12({{"version", "1.2"}});
  TestLoadBalancerContext context_13({{"version", "1.3"}});
  TestLoadBalancerContext context_default({{"default", "true"}});

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"default"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"default", "true"}});

  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  // Add hosts initial hosts.
  init({{"tcp://127.0.0.1:8000", {{"version", "1.2"}}},
        {"tcp://127.0.0.1:8001", {{"version", "1.0"}, {"default", "true"}}}});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));

  // Swap the default version.
  host_set_.hosts_[0]->metadata(buildMetadata("1.2", true));
  host_set_.hosts_[1]->metadata(buildMetadata("1.0"));

  host_set_.runCallbacks({}, {});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_13));

  // Bump 1.0 to 1.3, one subset should be removed.
  host_set_.hosts_[1]->metadata(buildMetadata("1.3"));

  // No hosts added nor removed, so we bypass modifyHosts().
  host_set_.runCallbacks({}, {});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));

  // Rollback from 1.3 to 1.0.
  host_set_.hosts_[1]->metadata(buildMetadata("1.0"));

  host_set_.runCallbacks({}, {});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_13));

  // Make 1.0 default again.
  host_set_.hosts_[1]->metadata(buildMetadata("1.0", true));
  host_set_.hosts_[0]->metadata(buildMetadata("1.2"));

  host_set_.runCallbacks({}, {});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));
}

TEST_P(SubsetLoadBalancerTest, EmptySubsetsPurged) {
  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector({"version"}),
                                                     makeSelector({"version", "stage"})};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  // Simple add and remove.
  init({{"tcp://127.0.0.1:8000", {{"version", "1.2"}}},
        {"tcp://127.0.0.1:8001", {{"version", "1.0"}, {"stage", "prod"}}}});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());

  host_set_.hosts_[0]->metadata(buildMetadataWithStage("1.3"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_removed_.value());

  // Move host that was in the version + stage subset into a new version only subset.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.4"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_removed_.value());

  // Create a new version + stage subset.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.5", "devel"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(7U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_removed_.value());

  // Now move it back to its original version + stage subset.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.0", "prod"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(9U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(6U, stats_.lb_subsets_removed_.value());

  // Finally, remove the original version + stage subset again.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.6"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(10U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(8U, stats_.lb_subsets_removed_.value());
}

TEST_P(SubsetLoadBalancerTest, EmptySubsetsPurgedCollapsed) {
  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector({"version"}),
                                                     makeSelector({"version", "stage"})};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  // Init subsets.
  init({{"tcp://127.0.0.1:8000", {{"version", "1.2"}}},
        {"tcp://127.0.0.1:8001", {{"version", "1.0"}, {"stage", "prod"}}}});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());

  // Get rid of 1.0.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.2", "prod"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());

  // Get rid of stage prod.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.2"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(1U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_removed_.value());

  // Add stage prod back.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.2", "prod"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_removed_.value());
}

TEST_P(SubsetLoadBalancerTest, EmptySubsetsPurgedVersionChanged) {
  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector({"version"}),
                                                     makeSelector({"version", "stage"})};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  // Init subsets.
  init({{"tcp://127.0.0.1:8000", {{"version", "1.2"}}},
        {"tcp://127.0.0.1:8001", {{"version", "1.0"}, {"stage", "prod"}}}});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());

  // Get rid of 1.0.
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.2", "prod"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(2U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());

  // Change versions.
  host_set_.hosts_[0]->metadata(buildMetadataWithStage("1.3"));
  host_set_.hosts_[1]->metadata(buildMetadataWithStage("1.4", "prod"));
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(7U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_removed_.value());
}

TEST_P(SubsetLoadBalancerTest, MetadataChangedHostsAddedRemoved) {
  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_12({{"version", "1.2"}});
  TestLoadBalancerContext context_13({{"version", "1.3"}});
  TestLoadBalancerContext context_14({{"version", "1.4"}});
  TestLoadBalancerContext context_default({{"default", "true"}});
  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"default", "true"}});

  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"default"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  // Add hosts initial hosts.
  init({{"tcp://127.0.0.1:8000", {{"version", "1.2"}}},
        {"tcp://127.0.0.1:8001", {{"version", "1.0"}, {"default", "true"}}}});
  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(3U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));

  // Swap the default version.
  host_set_.hosts_[0]->metadata(buildMetadata("1.2", true));
  host_set_.hosts_[1]->metadata(buildMetadata("1.0"));

  // Add a new host.
  modifyHosts({makeHost("tcp://127.0.0.1:8002", {{"version", "1.3"}})}, {});

  EXPECT_EQ(4U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_13));

  // Swap default again and remove the previous one.
  host_set_.hosts_[0]->metadata(buildMetadata("1.2"));
  host_set_.hosts_[1]->metadata(buildMetadata("1.0", true));

  modifyHosts({}, {host_set_.hosts_[2]});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));

  // Swap the default version once more, this time adding a new host and removing
  // the current default version.
  host_set_.hosts_[0]->metadata(buildMetadata("1.2", true));
  host_set_.hosts_[1]->metadata(buildMetadata("1.0"));

  modifyHosts({makeHost("tcp://127.0.0.1:8003", {{"version", "1.4"}})}, {host_set_.hosts_[1]});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_13));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_14));

  // Make 1.4 default, without hosts being added/removed.
  host_set_.hosts_[0]->metadata(buildMetadata("1.2"));
  host_set_.hosts_[1]->metadata(buildMetadata("1.4", true));

  host_set_.runCallbacks({}, {});

  EXPECT_EQ(3U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(5U, stats_.lb_subsets_created_.value());
  EXPECT_EQ(2U, stats_.lb_subsets_removed_.value());
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_12));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_default));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_13));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_14));
}

TEST_P(SubsetLoadBalancerTest, UpdateRemovingLastSubsetHost) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"stage", "version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"hardware"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"hardware"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"stage", "version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"stage", "version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  HostVector hosts;
  hosts.emplace_back(makeTestHost(info_, "tcp://127.0.0.1:80", simTime()));
  hosts.emplace_back(makeHost("tcp://127.0.0.1:81", {{"version", "1.0"}}));

  host_set_.hosts_ = hosts;
  host_set_.hosts_per_locality_ = makeHostsPerLocality({hosts});

  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;

  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"x"}, envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"x"}, envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, ZoneAwareFallbackDefaultSubset) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][3], lb_->chooseHost(nullptr));
}

TEST_F(SubsetLoadBalancerTest, ZoneAwareBalancesSubsets) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

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
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][3], lb_->chooseHost(&context));
}

TEST_F(SubsetLoadBalancerTest, DescribeMetadata) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));
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

TEST_F(SubsetLoadBalancerTest, DisabledLocalityWeightAwareness) {
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

  // We configure a weighted host set that heavily favors the second locality.
  configureWeightedHostSet(
      {
          {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
      },
      {
          {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
          {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
      },
      host_set_, {1, 100});

  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());

  TestLoadBalancerContext context({{"version", "1.1"}});

  // Since we don't respect locality weights, the first locality is selected.
  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(&context));
}

// Verifies that we do *not* invoke coarseHealth() on hosts when constructing the load balancer.
// Since health is modified concurrently from multiple threads, it is not safe to call on the worker
// threads.
TEST_F(SubsetLoadBalancerTest, DoesNotCheckHostHealth) {
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));

  auto mock_host = std::make_shared<MockHost>();
  HostVector hosts{mock_host};
  host_set_.hosts_ = hosts;

  EXPECT_CALL(*mock_host, weight()).WillRepeatedly(Return(1));

  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());
}

TEST_F(SubsetLoadBalancerTest, EnabledLocalityWeightAwareness) {
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, localityWeightAware()).WillRepeatedly(Return(true));

  // We configure a weighted host set that heavily favors the second locality.
  configureWeightedHostSet(
      {
          {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
      },
      {
          {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
          {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
      },
      host_set_, {1, 100});

  common_config_.mutable_locality_weighted_lb_config();
  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());

  TestLoadBalancerContext context({{"version", "1.1"}});

  // Since we respect locality weights, the second locality is selected.
  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(&context));
}

TEST_F(SubsetLoadBalancerTest, EnabledScaleLocalityWeights) {

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, localityWeightAware()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, scaleLocalityWeight()).WillRepeatedly(Return(true));

  // We configure a weighted host set is weighted equally between each locality.
  configureWeightedHostSet(
      {
          {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
      },
      {
          {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:83", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
      },
      host_set_, {50, 50});

  common_config_.mutable_locality_weighted_lb_config();
  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());
  TestLoadBalancerContext context({{"version", "1.1"}});

  // Since we scale the locality weights by number of hosts removed, we expect to see the second
  // locality to be selected less because we've excluded more hosts in that locality than in the
  // first.
  // The localities are split 50/50, but because of the scaling we expect to see 66/33 instead.
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][3], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][3], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][3], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][1], lb_->chooseHost(&context));
}

TEST_F(SubsetLoadBalancerTest, EnabledScaleLocalityWeightsRounding) {

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, localityWeightAware()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, scaleLocalityWeight()).WillRepeatedly(Return(true));

  // We configure a weighted host set where the locality weights are very low to test
  // that we are rounding computation instead of flooring it.
  configureWeightedHostSet(
      {
          {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
      },
      {
          {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:83", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:84", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:85", {{"version", "1.1"}}},
      },
      host_set_, {2, 2});

  common_config_.mutable_locality_weighted_lb_config();
  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());
  TestLoadBalancerContext context({{"version", "1.0"}});

  // We expect to see a 33/66 split because 2 * 1 / 2 = 1 and 2 * 3 / 4 = 1.5 -> 2
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][2], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(&context));
}

// Regression for bug where missing locality weights crashed scaling and locality aware subset LBs.
TEST_F(SubsetLoadBalancerTest, ScaleLocalityWeightsWithNoLocalityWeights) {
  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  EXPECT_CALL(subset_info_, isEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, localityWeightAware()).WillRepeatedly(Return(true));
  EXPECT_CALL(subset_info_, scaleLocalityWeight()).WillRepeatedly(Return(true));

  configureHostSet(
      {
          {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
          {"tcp://127.0.0.1:81", {{"version", "1.1"}}},
      },
      host_set_);

  lb_ = std::make_shared<SubsetLoadBalancer>(lb_type_, priority_set_, nullptr, stats_, stats_store_,
                                             runtime_, random_, subset_info_, ring_hash_lb_config_,
                                             maglev_lb_config_, round_robin_lb_config_,
                                             least_request_lb_config_, common_config_, simTime());
}

TEST_P(SubsetLoadBalancerTest, GaugesUpdatedOnDestroy) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
  });

  EXPECT_EQ(1U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(0U, stats_.lb_subsets_removed_.value());

  lb_ = nullptr;

  EXPECT_EQ(0U, stats_.lb_subsets_active_.value());
  EXPECT_EQ(1U, stats_.lb_subsets_removed_.value());
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorNoFallbackPerSelector) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({
      {"tcp://127.0.0.1:80", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:81", {{"version", "1.0"}}},
      {"tcp://127.0.0.1:82", {{"version", "1.1"}}},
      {"tcp://127.0.0.1:83", {{"version", "1.1"}}},
  });

  TestLoadBalancerContext context_10({{"version", "1.0"}});
  TestLoadBalancerContext context_11({{"version", "1.1"}});
  TestLoadBalancerContext context_12({{"version", "1.2"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_11));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_10));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_11));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_12));
  EXPECT_EQ(0U, stats_.lb_subsets_fallback_.value());
  EXPECT_EQ(4U, stats_.lb_subsets_selected_.value());
}

TEST_P(SubsetLoadBalancerTest, FallbackNotDefinedForIntermediateSelector) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"stage", "version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"version", "1.0"}, {"stage", "dev"}}},
        {"tcp://127.0.0.1:81", {{"version", "1.0"}, {"stage", "canary"}}}});

  TestLoadBalancerContext context_match_host0({{"stage", "dev"}});
  TestLoadBalancerContext context_stage_nx({{"stage", "test"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_match_host0));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_match_host0));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_stage_nx));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_stage_nx));
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorFallbackOverridesTopLevelOne) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"version", "unknown"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_value));
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorNoFallbackMatchesTopLevelOne) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"version", "unknown"}});

  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_value));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_unknown_value));
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorDefaultAnyFallbackPerSelector) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::DEFAULT_SUBSET),
      makeSelector(
          {"app"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT),
      makeSelector(
          {"foo"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"bar", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  // Add hosts initial hosts.
  init({{"tcp://127.0.0.1:81", {{"version", "0.0"}}},
        {"tcp://127.0.0.1:82", {{"version", "1.0"}}},
        {"tcp://127.0.0.1:83", {{"app", "envoy"}}},
        {"tcp://127.0.0.1:84", {{"foo", "abc"}, {"bar", "default"}}}});

  TestLoadBalancerContext context_ver_10({{"version", "1.0"}});
  TestLoadBalancerContext context_ver_nx({{"version", "x"}});
  TestLoadBalancerContext context_app({{"app", "envoy"}});
  TestLoadBalancerContext context_app_nx({{"app", "ngnix"}});
  TestLoadBalancerContext context_foo({{"foo", "abc"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_app_nx));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_app_nx));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_app));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_ver_nx));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_foo));
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorDefaultAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET));

  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"version", "default"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::DEFAULT_SUBSET)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({
      {"tcp://127.0.0.1:80", {{"version", "new"}}},
      {"tcp://127.0.0.1:81", {{"version", "default"}}},
  });

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(nullptr));

  HostSharedPtr added_host1 = makeHost("tcp://127.0.0.1:8000", {{"version", "new"}});
  HostSharedPtr added_host2 = makeHost("tcp://127.0.0.1:8001", {{"version", "default"}});

  TestLoadBalancerContext context_ver_nx({{"version", "x"}});

  modifyHosts({added_host1, added_host2}, {host_set_.hosts_.back()});

  EXPECT_EQ(added_host2, lb_->chooseHost(&context_ver_nx));
}

TEST_P(SubsetLoadBalancerTest, SubsetSelectorAnyAfterUpdate) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT)};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({
      {"tcp://127.0.0.1:81", {{"version", "1"}}},
      {"tcp://127.0.0.1:82", {{"version", "2"}}},
  });

  TestLoadBalancerContext context_ver_nx({{"version", "x"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_ver_nx));

  HostSharedPtr added_host1 = makeHost("tcp://127.0.0.1:83", {{"version", "3"}});

  modifyHosts({added_host1}, {host_set_.hosts_.back()});

  EXPECT_EQ(added_host1, lb_->chooseHost(&context_ver_nx));
}

TEST_P(SubsetLoadBalancerTest, FallbackForCompoundSelector) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  const ProtobufWkt::Struct default_subset = makeDefaultSubset({{"foo", "bar"}});
  EXPECT_CALL(subset_info_, defaultSubset()).WillRepeatedly(ReturnRef(default_subset));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED),
      makeSelector(
          {"version", "hardware", "stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK),
      makeSelector(
          {"version", "hardware"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::DEFAULT_SUBSET),
      makeSelector(
          {"version", "stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET,
          {"version"})};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  // Add hosts initial hosts.
  init({{"tcp://127.0.0.1:80", {{"version", "1.0"}, {"hardware", "c32"}}},
        {"tcp://127.0.0.1:81", {{"version", "1.0"}, {"hardware", "c32"}, {"foo", "bar"}}},
        {"tcp://127.0.0.1:82", {{"version", "2.0"}, {"hardware", "c32"}, {"stage", "dev"}}},
        {"tcp://127.0.0.1:83", {{"version", "2.0"}}}});

  TestLoadBalancerContext context_match_host0({{"version", "1.0"}, {"hardware", "c32"}});
  TestLoadBalancerContext context_ver_nx({{"version", "x"}, {"hardware", "c32"}});
  TestLoadBalancerContext context_stage_nx(
      {{"version", "2.0"}, {"hardware", "c32"}, {"stage", "x"}});
  TestLoadBalancerContext context_hardware_nx(
      {{"version", "2.0"}, {"hardware", "zzz"}, {"stage", "dev"}});
  TestLoadBalancerContext context_match_host2(
      {{"version", "2.0"}, {"hardware", "c32"}, {"stage", "dev"}});
  TestLoadBalancerContext context_ver_20({{"version", "2.0"}});
  TestLoadBalancerContext context_ver_stage_match_host2({{"version", "2.0"}, {"stage", "dev"}});
  TestLoadBalancerContext context_ver_stage_nx({{"version", "2.0"}, {"stage", "canary"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_match_host0));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_ver_nx));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_hardware_nx));
  EXPECT_EQ(nullptr, lb_->chooseHost(&context_stage_nx));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_match_host2));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_match_host2));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_ver_20));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_ver_stage_match_host2));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_ver_stage_match_host2));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&context_ver_stage_nx));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&context_ver_stage_nx));
}

TEST_P(SubsetLoadBalancerTest, KeysSubsetFallbackChained) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK),
      makeSelector(
          {"stage", "version"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET,
          {"stage"}),
      makeSelector(
          {"stage", "version", "hardware"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET,
          {"version", "stage"})};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"version", "1.0"}, {"hardware", "c32"}, {"stage", "dev"}}},
        {"tcp://127.0.0.1:81", {{"version", "2.0"}, {"hardware", "c64"}, {"stage", "dev"}}},
        {"tcp://127.0.0.1:82", {{"version", "1.0"}, {"hardware", "c32"}, {"stage", "test"}}}});

  TestLoadBalancerContext context_match_host0(
      {{"version", "1.0"}, {"hardware", "c32"}, {"stage", "dev"}});
  TestLoadBalancerContext context_hw_nx(
      {{"version", "2.0"}, {"hardware", "arm"}, {"stage", "dev"}});
  TestLoadBalancerContext context_ver_hw_nx(
      {{"version", "1.2"}, {"hardware", "arm"}, {"stage", "dev"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_match_host0));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_match_host0));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_hw_nx));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_hw_nx));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_ver_hw_nx));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_ver_hw_nx));
}

TEST_P(SubsetLoadBalancerTest, KeysSubsetFallbackToNotExistingSelector) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector(
      {"stage", "version"},
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET,
      {"stage"})};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"version", "1.0"}, {"stage", "dev"}}}});

  TestLoadBalancerContext context_nx({{"version", "1.0"}, {"stage", "test"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_nx));
  EXPECT_EQ(1U, stats_.lb_subsets_fallback_.value());
}

TEST_P(SubsetLoadBalancerTest, MetadataFallbackList) {
  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));
  EXPECT_CALL(subset_info_, metadataFallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::FALLBACK_LIST));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector({"version"})};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"version", "1.0"}}},
        {"tcp://127.0.0.1:81", {{"version", "2.0"}}},
        {"tcp://127.0.0.1:82", {{"version", "3.0"}}}});

  const auto version1_host = host_set_.hosts_[0];
  const auto version2_host = host_set_.hosts_[1];
  const auto version3_host = host_set_.hosts_[2];

  TestLoadBalancerContext context_with_fallback({{"fallback_list", valueFromJson(R""""(
    [
      {"version": "2.0"},
      {"version": "1.0"}
    ]
  )"""")}});

  // version 2.0 is preferred, should be selected
  EXPECT_EQ(version2_host, lb_->chooseHost(&context_with_fallback));
  EXPECT_EQ(version2_host, lb_->chooseHost(&context_with_fallback));
  EXPECT_EQ(version2_host, lb_->chooseHost(&context_with_fallback));

  modifyHosts({}, {version2_host});

  // version 1.0 is a fallback, should be used when host with version 2.0 is removed
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_with_fallback));
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_with_fallback));
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_with_fallback));

  // if fallback_list is not a list, it should be ignored
  // regular metadata is in effect
  ProtobufWkt::Value null_value;
  null_value.set_null_value(ProtobufWkt::NullValue::NULL_VALUE);
  TestLoadBalancerContext context_with_invalid_fallback_list_null(
      {{"version", valueFromJson("\"3.0\"")}, {"fallback_list", null_value}});

  EXPECT_EQ(version3_host, lb_->chooseHost(&context_with_invalid_fallback_list_null));
  EXPECT_EQ(version3_host, lb_->chooseHost(&context_with_invalid_fallback_list_null));

  // should ignore fallback list entry which is not a struct
  TestLoadBalancerContext context_with_invalid_fallback_list_entry(
      {{"fallback_list", valueFromJson(R""""(
        [
          "invalid string entry",
          {"version": "1.0"}
        ]
      )"""")}});

  EXPECT_EQ(version1_host, lb_->chooseHost(&context_with_invalid_fallback_list_entry));
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_with_invalid_fallback_list_entry));

  // simple metadata with no fallback should work as usual
  TestLoadBalancerContext context_no_fallback({{"version", "1.0"}});
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_no_fallback));
  EXPECT_EQ(version1_host, lb_->chooseHost(&context_no_fallback));

  // fallback metadata overrides regular metadata value
  TestLoadBalancerContext context_fallback_overrides_metadata_value(
      {{"version", valueFromJson("\"1.0\"")}, {"fallback_list", valueFromJson(R""""(
        [
          {"hardware": "arm"},
          {"version": "5.0"},
          {"version": "3.0"}
        ]
      )"""")}});
  EXPECT_EQ(version3_host, lb_->chooseHost(&context_fallback_overrides_metadata_value));
  EXPECT_EQ(version3_host, lb_->chooseHost(&context_fallback_overrides_metadata_value));
}

TEST_P(SubsetLoadBalancerTest, MetadataFallbackDisabled) {

  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));
  EXPECT_CALL(subset_info_, metadataFallbackPolicy())
      .WillRepeatedly(
          Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::METADATA_NO_FALLBACK));

  std::vector<SubsetSelectorPtr> subset_selectors = {makeSelector({"fallback_list"})};
  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"fallback_list", "lorem"}}},
        {"tcp://127.0.0.1:81", {{"fallback_list", "ipsum"}}}});

  // should treat 'fallback_list' as a regular metadata key
  TestLoadBalancerContext context({{"fallback_list", "ipsum"}});
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context));
}

TEST_P(SubsetLoadBalancerTest, MetadataFallbackAndSubsetFallback) {

  EXPECT_CALL(subset_info_, fallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  EXPECT_CALL(subset_info_, metadataFallbackPolicy())
      .WillRepeatedly(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::FALLBACK_LIST));

  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector(
          {"hardware"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NO_FALLBACK),
      makeSelector(
          {"hardware", "stage"},
          envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET,
          {"hardware"})};

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));

  init({{"tcp://127.0.0.1:80", {{"hardware", "c32"}, {"stage", "production"}}},
        {"tcp://127.0.0.1:81", {{"hardware", "c64"}, {"stage", "canary"}}},
        {"tcp://127.0.0.1:82", {{"hardware", "c64"}, {"stage", "production"}}}});

  const auto c32_production_host = host_set_.hosts_[0];
  const auto c64_canary_host = host_set_.hosts_[1];
  const auto c64_production_host = host_set_.hosts_[2];

  TestLoadBalancerContext context_canary_c32_preffered(
      {{"stage", valueFromJson("\"canary\"")}, {"fallback_list", valueFromJson(R""""(
        [
          {"hardware": "c32"},
          {"hardware": "c64"}
        ]
      )"""")}});

  // Should select c32_production_host using first fallback entry, even
  // when it doesn't match on requested 'stage' - because of the subset fallback policy.
  // There is the c64_canary_host which exactly matches second fallback entry, but
  // that entry is not used.
  EXPECT_EQ(c32_production_host, lb_->chooseHost(&context_canary_c32_preffered));
  EXPECT_EQ(c32_production_host, lb_->chooseHost(&context_canary_c32_preffered));

  TestLoadBalancerContext context_canary_c16_preffered(
      {{"stage", valueFromJson("\"canary\"")}, {"fallback_list", valueFromJson(R""""(
        [
          {"hardware": "c16"},
          {"hardware": "c64"}
        ]
      )"""")}});

  // Should select c64_canary_host using second fallback entry. First fallback
  // entry doesn't match anything even considering subset fallback policy.
  EXPECT_EQ(c64_canary_host, lb_->chooseHost(&context_canary_c16_preffered));
  EXPECT_EQ(c64_canary_host, lb_->chooseHost(&context_canary_c16_preffered));

  TestLoadBalancerContext context_unknown_or_c64({{"fallback_list", valueFromJson(R""""(
    [
      {"unknown": "ipsum"},
      {"hardware": "c64"}
    ]
  )"""")}});

  // should select any host using first fallback entry, because of ANY_ENDPOINT
  // subset fallback policy
  EXPECT_EQ(c32_production_host, lb_->chooseHost(&context_unknown_or_c64));
  EXPECT_EQ(c64_canary_host, lb_->chooseHost(&context_unknown_or_c64));
  EXPECT_EQ(c64_production_host, lb_->chooseHost(&context_unknown_or_c64));
}

INSTANTIATE_TEST_SUITE_P(UpdateOrderings, SubsetLoadBalancerTest,
                         testing::ValuesIn({UpdateOrder::RemovesFirst, UpdateOrder::Simultaneous}));

class SubsetLoadBalancerSingleHostPerSubsetTest : public SubsetLoadBalancerTest {
public:
  SubsetLoadBalancerSingleHostPerSubsetTest()
      : default_subset_selectors_({
            makeSelector({"key"}, true),
        }) {
    ON_CALL(subset_info_, subsetSelectors()).WillByDefault(ReturnRef(default_subset_selectors_));
    ON_CALL(subset_info_, fallbackPolicy())
        .WillByDefault(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  }

  using SubsetLoadBalancerTest::init;
  void init() {
    init({
        {"tcp://127.0.0.1:80", {}},
        {"tcp://127.0.0.1:81", {{"key", "a"}}},
        {"tcp://127.0.0.1:82", {{"key", "b"}}},

    });
  }

  using SubsetLoadBalancerTest::makeSelector;
  SubsetSelectorPtr makeSelector(const std::set<std::string>& selector_keys,
                                 bool single_host_per_subset) {
    return makeSelector(
        selector_keys,
        envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED, {},
        single_host_per_subset);
  }

  std::vector<SubsetSelectorPtr> default_subset_selectors_;
};

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, AcceptMultipleSelectors) {
  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector({"version"}, false),
      makeSelector({"stage"}, true),
  };

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  ON_CALL(subset_info_, fallbackPolicy())
      .WillByDefault(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  init({
      {"tcp://127.0.0.1:80", {}},
      {"tcp://127.0.0.1:81", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:82", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:83", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:84", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:85", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:86", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:87", {{"version", "v2"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:88", {{"version", "v2"}, {"stage", "prod"}}},
  });

  TestLoadBalancerContext version_v1({{"version", "v1"}});
  TestLoadBalancerContext version_v2({{"version", "v2"}});
  TestLoadBalancerContext stage_dev({{"stage", "dev"}});
  TestLoadBalancerContext stage_prod({{"stage", "prod"}});
  TestLoadBalancerContext stage_test({{"stage", "test"}});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&version_v1));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&version_v1));

  EXPECT_EQ(host_set_.hosts_[5], lb_->chooseHost(&version_v2));
  EXPECT_EQ(host_set_.hosts_[6], lb_->chooseHost(&version_v2));

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&stage_dev));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&stage_dev));

  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&stage_prod));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&stage_prod));

  EXPECT_EQ(nullptr, lb_->chooseHost(&stage_test));
}

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, AcceptMultipleKeys) {
  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector({"version", "stage"}, true),
  };

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  ON_CALL(subset_info_, fallbackPolicy())
      .WillByDefault(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  init({
      {"tcp://127.0.0.1:80", {}},
      {"tcp://127.0.0.1:81", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:82", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:83", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:84", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:85", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:86", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:87", {{"version", "v2"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:88", {{"version", "v2"}, {"stage", "prod"}}},
  });

  TestLoadBalancerContext v1_dev({{"version", "v1"}, {"stage", "dev"}});
  TestLoadBalancerContext v1_prod({{"version", "v1"}, {"stage", "prod"}});
  TestLoadBalancerContext v2_dev({{"version", "v2"}, {"stage", "dev"}});
  TestLoadBalancerContext v2_prod({{"version", "v2"}, {"stage", "prod"}});
  TestLoadBalancerContext v2_test({{"version", "v2"}, {"stage", "test"}});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&v1_dev));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&v1_prod));
  EXPECT_EQ(host_set_.hosts_[5], lb_->chooseHost(&v2_dev));
  EXPECT_EQ(host_set_.hosts_[7], lb_->chooseHost(&v2_prod));
  EXPECT_EQ(nullptr, lb_->chooseHost(&v2_test));
}

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, HybridMultipleSelectorsAndKeys) {
  std::vector<SubsetSelectorPtr> subset_selectors = {
      makeSelector({"version", "stage"}, true),
      makeSelector({"stage"}, false),
  };

  EXPECT_CALL(subset_info_, subsetSelectors()).WillRepeatedly(ReturnRef(subset_selectors));
  ON_CALL(subset_info_, fallbackPolicy())
      .WillByDefault(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK));

  init({
      {"tcp://127.0.0.1:80", {}},
      {"tcp://127.0.0.1:81", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:82", {{"version", "v1"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:83", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:84", {{"version", "v1"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:85", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:86", {{"version", "v2"}, {"stage", "dev"}}},
      {"tcp://127.0.0.1:87", {{"version", "v2"}, {"stage", "prod"}}},
      {"tcp://127.0.0.1:88", {{"version", "v2"}, {"stage", "prod"}}},
  });

  TestLoadBalancerContext v1_dev({{"version", "v1"}, {"stage", "dev"}});
  TestLoadBalancerContext v1_prod({{"version", "v1"}, {"stage", "prod"}});
  TestLoadBalancerContext v2_dev({{"version", "v2"}, {"stage", "dev"}});
  TestLoadBalancerContext v2_prod({{"version", "v2"}, {"stage", "prod"}});
  TestLoadBalancerContext v2_test({{"version", "v2"}, {"stage", "test"}});
  TestLoadBalancerContext stage_dev({{"stage", "dev"}});
  TestLoadBalancerContext stage_prod({{"stage", "prod"}});
  TestLoadBalancerContext stage_test({{"stage", "test"}});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&v1_dev));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&v1_prod));
  EXPECT_EQ(host_set_.hosts_[5], lb_->chooseHost(&v2_dev));
  EXPECT_EQ(host_set_.hosts_[7], lb_->chooseHost(&v2_prod));
  EXPECT_EQ(nullptr, lb_->chooseHost(&v2_test));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&stage_dev));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&stage_dev));
  EXPECT_EQ(host_set_.hosts_[3], lb_->chooseHost(&stage_prod));
  EXPECT_EQ(host_set_.hosts_[4], lb_->chooseHost(&stage_prod));
  EXPECT_EQ(nullptr, lb_->chooseHost(&stage_test));
}

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, DuplicateMetadataStat) {
  init({
      {"tcp://127.0.0.1:80", {{"key", "a"}}},
      {"tcp://127.0.0.1:81", {{"key", "a"}}},
      {"tcp://127.0.0.1:82", {{"key", "a"}}},
      {"tcp://127.0.0.1:83", {{"key", "b"}}},
  });
  // The first 'a' is the original, the next 2 instances of 'a' are duplicates (counted
  // in stat), and 'b' is another non-duplicate.
  for (auto& gauge : stats_store_.gauges()) {
    ENVOY_LOG_MISC(debug, "name {} value {}", gauge->name(), gauge->value());
  }
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "testprefix.lb_subsets_single_host_per_subset_duplicate")
                   ->value());
}

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, Match) {
  init();

  TestLoadBalancerContext host_1({{"key", "a"}});
  TestLoadBalancerContext host_2({{"key", "b"}});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_1));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_1));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_2));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_2));
}

TEST_F(SubsetLoadBalancerSingleHostPerSubsetTest, FallbackOnUnknownMetadata) {
  init();

  TestLoadBalancerContext context_unknown_key({{"unknown", "unknown"}});
  TestLoadBalancerContext context_unknown_value({{"key", "unknown"}});

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&context_unknown_key));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&context_unknown_value));
}

TEST_P(SubsetLoadBalancerSingleHostPerSubsetTest, Update) {
  init();

  TestLoadBalancerContext host_a({{"key", "a"}});
  TestLoadBalancerContext host_b({{"key", "b"}});
  TestLoadBalancerContext host_c({{"key", "c"}});
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_a));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_a));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_b));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_b));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&host_c)); // fallback
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_c)); // fallback

  HostSharedPtr added_host = makeHost("tcp://127.0.0.1:8000", {{"key", "c"}});

  // Remove b, add c
  modifyHosts({added_host}, {host_set_.hosts_.back()});

  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_a));
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_a));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_c));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_c));
  EXPECT_EQ(host_set_.hosts_[2], lb_->chooseHost(&host_b)); // fallback
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(&host_b)); // fallback
  EXPECT_EQ(host_set_.hosts_[1], lb_->chooseHost(&host_b)); // fallback
}

INSTANTIATE_TEST_SUITE_P(UpdateOrderings, SubsetLoadBalancerSingleHostPerSubsetTest,
                         testing::ValuesIn({UpdateOrder::RemovesFirst, UpdateOrder::Simultaneous}));

} // namespace SubsetLoadBalancerTest
} // namespace Upstream
} // namespace Envoy
