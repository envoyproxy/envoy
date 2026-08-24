#include "source/common/singleton/manager_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class WrapperedProvider {
public:
  WrapperedProvider(ShareProviderSharedPtr provider) : provider_(provider) {}

  uint32_t tokensPerFill(uint32_t origin_tokens_per_fill) const {
    return std::ceil(origin_tokens_per_fill * provider_->getTokensShareFactor());
  }

  ShareProviderSharedPtr provider_;
};

TEST(ShareProviderManagerTest, ShareProviderManagerTest) {
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Stats::TestUtil::TestStore store;
  Singleton::ManagerImpl manager;

  NiceMock<Upstream::MockPrioritySet> priority_set;
  cm.local_cluster_name_ = "local_cluster";
  cm.initializeClusters({"local_cluster"}, {});

  const auto* mock_local_cluster = cm.active_clusters_.at("local_cluster").get();

  EXPECT_CALL(*mock_local_cluster, prioritySet()).WillOnce(ReturnRef(priority_set));
  EXPECT_CALL(priority_set, addMemberUpdateCb(_));

  // Set the membership total to 2.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(2);

  ShareProviderManagerSharedPtr share_provider_manager = ShareProviderManager::singleton(
      dispatcher, cm, manager, local_info, *store.rootScope());
  EXPECT_NE(share_provider_manager, nullptr);

  auto provider = std::make_shared<WrapperedProvider>(
      share_provider_manager->getShareProvider(ProtoLocalClusterRateLimit()));

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(1, provider->tokensPerFill(2));
  EXPECT_EQ(2, provider->tokensPerFill(4));
  EXPECT_EQ(4, provider->tokensPerFill(8));

  // Set the membership total to 4.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(4);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(1, provider->tokensPerFill(4));
  EXPECT_EQ(2, provider->tokensPerFill(8));
  EXPECT_EQ(4, provider->tokensPerFill(16));

  // Set the membership total to 0.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(0);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));

  // Set the membership total to 1.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(1);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));

  // Destroy the share provider manager.
  // This is used to ensure the share provider is still safe to use even
  // the share provider manager is destroyed. But note this should never
  // happen in real production because the share provider manager should
  // have longer life cycle than the limiter.
  share_provider_manager.reset();

  // Set the membership total to 4 again.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(4);
  priority_set.runUpdateCallbacks(0, {}, {});

  // The provider should still work but the value should not change.
  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));
}

class WeightedShareProviderManagerTest : public testing::Test {
public:
  WeightedShareProviderManagerTest() {
    cm_.local_cluster_name_ = "local_cluster";
    cm_.initializeClusters({"local_cluster"}, {});
    mock_local_cluster_ = cm_.active_clusters_.at("local_cluster").get();

    EXPECT_CALL(*mock_local_cluster_, prioritySet()).WillRepeatedly(ReturnRef(priority_set_));
  }

  // Places `weights` on hosts 10.0.0.1, 10.0.0.2, ... of the local cluster's given priority.
  void setHosts(uint32_t priority, const std::vector<uint32_t>& weights) {
    auto* host_set = priority_set_.getMockHostSet(priority);
    host_set->hosts_.clear();
    for (uint32_t i = 0; i < weights.size(); i++) {
      host_set->hosts_.push_back(Upstream::makeTestHost(
          cluster_info_, fmt::format("tcp://10.0.0.{}:80", i + 1), weights[i]));
    }
  }

  // Sets the node metadata that carries this instance's own weight.
  void setSelfWeightMetadata(const Protobuf::Value& value) {
    auto& namespace_fields =
        *(*local_info_.node_.mutable_metadata()->mutable_fields())["envoy.local_ratelimit"]
             .mutable_struct_value()
             ->mutable_fields();
    namespace_fields["self_weight"] = value;
  }

  void setSelfWeight(double weight) {
    Protobuf::Value value;
    value.set_number_value(weight);
    setSelfWeightMetadata(value);
  }

  void createManager() {
    share_provider_manager_ = ShareProviderManager::singleton(dispatcher_, cm_, manager_,
                                                             local_info_, *store_.rootScope());
    ASSERT_NE(share_provider_manager_, nullptr);
  }

  WrapperedProvider weightedProvider() {
    ProtoLocalClusterRateLimit config;
    config.set_share_mode(ProtoLocalClusterRateLimit::WEIGHTED);
    return WrapperedProvider(share_provider_manager_->getShareProvider(config));
  }

  // Absent until a WEIGHTED provider is asked for, since the monitor that owns it is created on
  // first use.
  absl::optional<uint64_t> selfWeightNotFound() {
    const auto gauge =
        store_.findGaugeByString("local_rate_limit.local_cluster_share.self_weight_not_found");
    if (!gauge.has_value()) {
      return absl::nullopt;
    }
    return gauge->get().value();
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Stats::TestUtil::TestStore store_;
  Singleton::ManagerImpl manager_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  const Upstream::MockCluster* mock_local_cluster_{};
  ShareProviderManagerSharedPtr share_provider_manager_;
};

// The weighted share is this instance's own weight over the total weight of the local cluster.
TEST_F(WeightedShareProviderManagerTest, WeightedShare) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  createManager();

  auto provider = weightedProvider();

  // 3 of 4 total weight.
  EXPECT_EQ(3, provider.tokensPerFill(4));
  EXPECT_EQ(6, provider.tokensPerFill(8));
  EXPECT_EQ(1, provider.tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(0UL, selfWeightNotFound());

  // Scale the fleet up. This instance's own weight is unchanged, so it is now 3 of 10.
  setHosts(0, {1, 3, 4, 2});
  priority_set_.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(3, provider.tokensPerFill(10));
  EXPECT_EQ(6, provider.tokensPerFill(20));

  // A weight change elsewhere in the cluster is picked up too: EDS reports weight changes through
  // the same membership update callback, with no hosts added or removed.
  setHosts(0, {1, 3, 4, 8});
  priority_set_.runUpdateCallbacks(0, {}, {});

  // Now 3 of 16.
  EXPECT_EQ(3, provider.tokensPerFill(16));
}

// The share is correct as soon as the provider is handed out, without waiting for the next
// membership update.
TEST_F(WeightedShareProviderManagerTest, ProviderIsPrimedOnCreation) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  createManager();

  EXPECT_EQ(3, weightedProvider().tokensPerFill(4));
}

// The weighted monitor, and so its gauge, is only created once a WEIGHTED provider is asked for.
TEST_F(WeightedShareProviderManagerTest, WeightedMonitorIsCreatedOnFirstUse) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  createManager();

  EXPECT_EQ(absl::nullopt, selfWeightNotFound());

  weightedProvider();

  EXPECT_EQ(0UL, selfWeightNotFound());
}

// Every WEIGHTED configuration shares one monitor, since none of them varies what the share is.
TEST_F(WeightedShareProviderManagerTest, WeightedConfigsShareOneProvider) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  createManager();

  EXPECT_EQ(weightedProvider().provider_, weightedProvider().provider_);
}

// The total weight is summed across all priorities, matching what the even share mode counts.
TEST_F(WeightedShareProviderManagerTest, WeightedShareAcrossPriorities) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  setHosts(1, {4});
  createManager();

  auto provider = weightedProvider();

  // 3 of 8 total weight.
  EXPECT_EQ(3, provider.tokensPerFill(8));
}

// With uniform weights, WEIGHTED reduces exactly to EVEN.
TEST_F(WeightedShareProviderManagerTest, UniformWeightsMatchEvenShare) {
  setSelfWeight(32);
  setHosts(0, {32, 32, 32, 32});
  mock_local_cluster_->info_->endpoint_stats_.membership_total_.set(4);
  createManager();

  auto weighted = weightedProvider();
  auto even =
      WrapperedProvider(share_provider_manager_->getShareProvider(ProtoLocalClusterRateLimit()));

  EXPECT_EQ(even.tokensPerFill(8), weighted.tokensPerFill(8));
  EXPECT_EQ(2, weighted.tokensPerFill(8));
}

// An instance that was never given its own weight takes the smallest weight in the cluster, which
// is no larger than the share of any instance.
TEST_F(WeightedShareProviderManagerTest, MissingSelfWeightFallsBackToSmallestWeight) {
  setHosts(0, {1, 3, 4, 2});
  createManager();

  auto provider = weightedProvider();

  // 1 of 10, not 1/4 of the bucket and not all of it.
  EXPECT_EQ(1, provider.tokensPerFill(10));
  EXPECT_EQ(2, provider.tokensPerFill(20));
  EXPECT_EQ(1UL, selfWeightNotFound());

  // The fallback keeps tracking the cluster.
  setHosts(0, {2, 6});
  priority_set_.runUpdateCallbacks(0, {}, {});
  EXPECT_EQ(2, provider.tokensPerFill(8));
}

// A weight that is not a number is not a weight.
TEST_F(WeightedShareProviderManagerTest, NonNumericSelfWeightFallsBackToSmallestWeight) {
  Protobuf::Value value;
  value.set_string_value("3");
  setSelfWeightMetadata(value);
  setHosts(0, {1, 3});
  createManager();

  EXPECT_EQ(4, weightedProvider().tokensPerFill(16));
  EXPECT_EQ(1UL, selfWeightNotFound());
}

// Neither is a non-positive one.
TEST_F(WeightedShareProviderManagerTest, NonPositiveSelfWeightFallsBackToSmallestWeight) {
  setSelfWeight(0);
  setHosts(0, {1, 3});
  createManager();

  EXPECT_EQ(4, weightedProvider().tokensPerFill(16));
  EXPECT_EQ(1UL, selfWeightNotFound());
}

// A stale weight left behind by a larger topology yields the whole bucket rather than more than it.
TEST_F(WeightedShareProviderManagerTest, SelfWeightAboveTotalYieldsWholeBucket) {
  setSelfWeight(100);
  setHosts(0, {1, 3});
  createManager();

  auto provider = weightedProvider();

  EXPECT_EQ(8, provider.tokensPerFill(8));
  // This is not the fallback: the weight was found, it is just too large.
  EXPECT_EQ(0UL, selfWeightNotFound());
}

// An empty local cluster leaves the bucket undivided, matching the even share mode.
TEST_F(WeightedShareProviderManagerTest, EmptyLocalCluster) {
  setSelfWeight(3);
  setHosts(0, {});
  createManager();

  EXPECT_EQ(8, weightedProvider().tokensPerFill(8));
}

// An explicit EVEN share mode is the same provider that an unset share mode returns, and it ignores
// host weights.
TEST_F(WeightedShareProviderManagerTest, ExplicitEvenShareModeIgnoresWeights) {
  setSelfWeight(3);
  setHosts(0, {1, 3});
  mock_local_cluster_->info_->endpoint_stats_.membership_total_.set(2);
  createManager();

  ProtoLocalClusterRateLimit config;
  config.set_share_mode(ProtoLocalClusterRateLimit::EVEN);
  EXPECT_EQ(share_provider_manager_->getShareProvider(config),
            share_provider_manager_->getShareProvider(ProtoLocalClusterRateLimit()));

  auto provider = WrapperedProvider(share_provider_manager_->getShareProvider(config));
  EXPECT_EQ(4, provider.tokensPerFill(8));
}

class MockShareProvider : public ShareProvider {
public:
  MockShareProvider() = default;
  MOCK_METHOD(double, getTokensShareFactor, (), (const));
};

class LocalRateLimiterImplTest : public testing::Test {
public:
  void initializeWithAtomicTokenBucket(const std::chrono::milliseconds fill_interval,
                                       const uint32_t max_tokens, const uint32_t tokens_per_fill,
                                       ShareProviderSharedPtr share_provider = nullptr) {
    rate_limiter_ =
        std::make_shared<LocalRateLimiterImpl>(fill_interval, max_tokens, tokens_per_fill,
                                               dispatcher_, descriptors_, true, share_provider);
  }

  Envoy::Protobuf::RepeatedPtrField<
      envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>
      descriptors_;

  std::vector<Envoy::RateLimit::Descriptor> route_descriptors_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<LocalRateLimiterImpl> rate_limiter_;
};

// Make sure we fail with a fill rate this is too fast.
TEST_F(LocalRateLimiterImplTest, TooFastFillRate) {
  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(49), 100, 1, dispatcher_, descriptors_),
      EnvoyException, "local rate limit token bucket fill timer must be >= 50ms");
}

// Verify that max_tokens=0 skips the 50ms fill interval validation since fill is irrelevant.
TEST_F(LocalRateLimiterImplTest, ZeroMaxTokensSkipsFillIntervalCheck) {
  VERBOSE_EXPECT_NO_THROW(
      LocalRateLimiterImpl(std::chrono::milliseconds(10), 0, 1, dispatcher_, descriptors_));
}

class LocalRateLimiterDescriptorImplTest : public LocalRateLimiterImplTest {
public:
  void initializeWithAtomicTokenBucketDescriptor(const std::chrono::milliseconds fill_interval,
                                                 const uint32_t max_tokens,
                                                 const uint32_t tokens_per_fill,
                                                 uint32_t lru_size = 20) {
    rate_limiter_ =
        std::make_shared<LocalRateLimiterImpl>(fill_interval, max_tokens, tokens_per_fill,
                                               dispatcher_, descriptors_, true, nullptr, lru_size);
  }

  static constexpr absl::string_view single_descriptor_config_yaml = R"(
  entries:
  - key: foo2
    value: bar2
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
  )";

  static constexpr absl::string_view wildcard_descriptor_config_yaml = R"(
  entries:
  - key: user
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
  )";

  static constexpr absl::string_view multiple_wildcard_descriptor_config_yaml = R"(
  entries:
  - key: user
  - key: org
    value: test
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
  )";

  static constexpr absl::string_view multiple_descriptor_config_yaml = R"(
  entries:
  - key: hello
    value: world
  - key: foo
    value: bar
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
  )";

  // Default token bucket
  std::vector<RateLimit::Descriptor> descriptor_{{{{"foo2", "bar2"}}}};
  std::vector<RateLimit::Descriptor> descriptor2_{{{{"hello", "world"}, {"foo", "bar"}}}};
  std::vector<RateLimit::Descriptor> no_match_descriptor_{{{{"no_match", "no_match"}}}};
};

// Make sure error raised in case duplicate/replicated descriptors are found.
TEST_F(LocalRateLimiterImplTest, DuplicatedDynamicTokenBucketDescriptor) {
  TestUtility::loadFromYaml(
      fmt::format(LocalRateLimiterDescriptorImplTest::wildcard_descriptor_config_yaml, 2, 1, "60s"),
      *descriptors_.Add());
  TestUtility::loadFromYaml(
      fmt::format(LocalRateLimiterDescriptorImplTest::wildcard_descriptor_config_yaml, 2, 1, "60s"),
      *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(LocalRateLimiterImpl(std::chrono::milliseconds(60000), 2, 1,
                                                 dispatcher_, descriptors_, true, nullptr, 1),

                            EnvoyException,
                            "duplicate descriptor in the local rate descriptor: user=");
}

// Verify dynamic token bucket functionality with a single entry descriptor.
TEST_F(LocalRateLimiterDescriptorImplTest, DynamicTokenBuckets) {
  TestUtility::loadFromYaml(fmt::format(wildcard_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 20, 2, 1);

  std::vector<RateLimit::Descriptor> descriptors{{{{"user", "A"}}}};

  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);

  std::vector<RateLimit::Descriptor> descriptors2{{{{"user", "B"}}}};
  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors2).allowed);

  // this must not be rate-limited because it will be handled by default bucket which uses
  // max_tokens i.e 20
  std::vector<RateLimit::Descriptor> extra_entries_descriptor{{{{"user", "C"}, {"key", "value"}}}};
  EXPECT_TRUE(rate_limiter_->requestAllowed(extra_entries_descriptor).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(extra_entries_descriptor).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(extra_entries_descriptor).allowed);

  // this must not be rate-limited because it will be handled by default bucket which uses
  // max_tokens i.e 20
  std::vector<RateLimit::Descriptor> different_key_descriptor{{{{"notuser", "A"}}}};
  EXPECT_TRUE(rate_limiter_->requestAllowed(different_key_descriptor).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(different_key_descriptor).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(different_key_descriptor).allowed);
}

// Verify dynamic token bucket functionality with multiple entries descriptor.
TEST_F(LocalRateLimiterDescriptorImplTest, DynamicTokenBucketswildcardWithMultipleEntries) {
  TestUtility::loadFromYaml(fmt::format(multiple_wildcard_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 20, 2, 1);

  std::vector<RateLimit::Descriptor> descriptors{{{{"user", "A"}}}};
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 2 -> 2 tokens. This is to check if the tokens are not consumed
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);

  // same size entries but non-matching key for wildcard descriptor. Should not be rate-limited.
  std::vector<RateLimit::Descriptor> descriptors2{{{{"user", "A"}, {"key", "value"}}}};
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 2 -> 2 tokens. This is to check if the tokens are not consumed
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);

  // same size entries but non-wildcard key's values does not match. Should not be rate-limited.
  std::vector<RateLimit::Descriptor> descriptors3{{{{"user", "A"}, {"org", "not-test"}}}};
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors3).allowed);
  // Descriptor from 2 -> 2 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors3).allowed);
  // Descriptor from 2 -> 2 tokens. This is to check if the tokens are not consumed
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors3).allowed);

  // this must be rate-limited because non-wildcard key-value matches and wildcard key matches
  std::vector<RateLimit::Descriptor> descriptors4{{{{"user", "A"}, {"org", "test"}}}};
  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors4).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors4).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors4).allowed);
}

TEST_F(LocalRateLimiterDescriptorImplTest, DynamicTokenBucketsMixedRequestOrder) {
  TestUtility::loadFromYaml(fmt::format(wildcard_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 4, 2, 2);

  std::vector<RateLimit::Descriptor> descriptors{{{{"user", "A"}}}};
  std::vector<RateLimit::Descriptor> descriptors2{{{{"user", "B"}}}};

  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors2).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors2).allowed);
}

// Verify descriptor rate limit time with small fill interval is rejected.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorRateLimitSmallFillInterval) {
  // Set fill interval to 10 milliseconds.
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 10, 10, "0.010s"),
                            *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(59000), 2, 1, dispatcher_, descriptors_),
      EnvoyException, "local rate limit descriptor token bucket fill timer must be >= 50ms");
}

// Verify that max_tokens=0 always rejects for both default and per-descriptor token buckets.
TEST_F(LocalRateLimiterDescriptorImplTest, AlwaysRejectWithZeroMaxTokens) {
  // Default bucket: max_tokens=0 always rejects regardless of time advancing.
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 0, 1);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // Per-descriptor bucket: max_tokens=0 always rejects regardless of time advancing.
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 0, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify that a descriptor with max_tokens=0 skips the 50ms fill interval validation.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorZeroMaxTokensSkipsFillIntervalCheck) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 0, 1, "0.010s"),
                            *descriptors_.Add());
  VERBOSE_EXPECT_NO_THROW(
      LocalRateLimiterImpl(std::chrono::milliseconds(50), 1, 1, dispatcher_, descriptors_));
}

// Verify that a dynamic (wildcard) descriptor with max_tokens=0 always rejects.
TEST_F(LocalRateLimiterDescriptorImplTest, DynamicDescriptorAlwaysRejectWithZeroMaxTokens) {
  TestUtility::loadFromYaml(fmt::format(wildcard_descriptor_config_yaml, 0, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);

  std::vector<RateLimit::Descriptor> descriptors{{{{"user", "A"}}}};
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);

  // A different wildcard value also always rejects.
  std::vector<RateLimit::Descriptor> descriptors2{{{{"user", "B"}}}};
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors2).allowed);
}

TEST_F(LocalRateLimiterDescriptorImplTest, DuplicateDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(50), 1, 1, dispatcher_, descriptors_),
      EnvoyException, "duplicate descriptor in the local rate descriptor: foo2=bar2");
}

// Verify no exception for per route config without descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorRateLimitNoExceptionWithoutDescriptor) {
  VERBOSE_EXPECT_NO_THROW(
      LocalRateLimiterImpl(std::chrono::milliseconds(59000), 2, 1, dispatcher_, descriptors_));
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucket) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMultipleTokensPerFill) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 1 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1 and
// share provider is used.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMultipleTokensPerFillWithShareProvider) {
  auto share_provider = std::make_shared<MockShareProvider>();
  EXPECT_CALL(*share_provider, getTokensShareFactor())
      .WillRepeatedly(testing::Invoke([]() -> double { return 0.5; }));

  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 2, share_provider);

  // Every request will consume 1 / factor = 2 tokens.

  // The limiter will be initialized with max tokens and will be consumed at once.
  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMaxTokensGreaterThanTokensPerFill) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 1, nullptr);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketStatus) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(3000), 2, 2);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(route_descriptors_);
  EXPECT_TRUE(rate_limit_result.allowed);

  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1500));

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(3000));

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
}

TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptorBase) {

  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);

  descriptor_[0].x_ratelimit_option_ = envoy::config::route::v3::RateLimit::DRAFT_VERSION_03;
  auto result = rate_limiter_->requestAllowed(descriptor_);
  EXPECT_TRUE(result.allowed);
  EXPECT_EQ(result.x_ratelimit_option, envoy::config::route::v3::RateLimit::DRAFT_VERSION_03);

  descriptor_[0].x_ratelimit_option_ = envoy::config::route::v3::RateLimit::OFF;
  result = rate_limiter_->requestAllowed(descriptor_);
  EXPECT_FALSE(result.allowed);
  EXPECT_EQ(result.x_ratelimit_option, envoy::config::route::v3::RateLimit::OFF);

  descriptor_[0].x_ratelimit_option_ = envoy::config::route::v3::RateLimit::UNSPECIFIED;
  result = rate_limiter_->requestAllowed(descriptor_);
  EXPECT_FALSE(result.allowed);
  EXPECT_EQ(result.x_ratelimit_option, envoy::config::route::v3::RateLimit::UNSPECIFIED);
}

TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with request per unit > 1.
TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketMultipleTokensPerFillDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 1 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(50));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest,
       AtomicTokenBucketDifferentDescriptorDifferentRateLimits) {
  TestUtility::loadFromYaml(fmt::format(multiple_descriptor_config_yaml, 1, 1, "1s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "2s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 3, 3);

  // 1 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // 1 -> 0 tokens for descriptor2_ and 0 only for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors sorted.
TEST_F(LocalRateLimiterDescriptorImplTest,
       AtomicTokenBucketDifferentDescriptorDifferentRateLimitsSorted) {
  TestUtility::loadFromYaml(fmt::format(multiple_descriptor_config_yaml, 1, 1, "1s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 3, 3);

  std::vector<RateLimit::Descriptor> descriptors{{{{"hello", "world"}, {"foo", "bar"}}},
                                                 {{{"foo2", "bar2"}}}};

  // Descriptors are sorted as descriptor2 < descriptor < global
  // Descriptor2 from 1 -> 0 tokens
  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Request limited by descriptor2 and won't consume tokens from descriptor.
  // Descriptor2 from 0 -> 0 tokens
  // Descriptor from 1 -> 1 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest,
       AtomicTokenBucketDifferentDescriptorDifferentRateLimitsDifferentStep) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 5, 5, "2s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(multiple_descriptor_config_yaml, 4, 4, "1s"),
                            *descriptors_.Add());

  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 8, 8);

  std::vector<RateLimit::Descriptor> descriptors;
  descriptors.push_back(descriptor_[0]);
  descriptors.push_back(descriptor2_[0]);
  descriptors[0].hits_addend_ = 2;
  descriptors[1].hits_addend_ = 3;

  // 5 -> 3 tokens for single_entry_descriptor_.
  // 4 -> 1 tokens for double_entry_descriptor_.
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);

  // 3 -> 1 tokens for single_entry_descriptor_. Note the single_entry_descriptor_ will
  // be consumed first because it has a lower fill rate.
  // double_entry_descriptor_ is not consumed because there is no enough tokens.
  auto result = rate_limiter_->requestAllowed(descriptors);
  EXPECT_FALSE(result.allowed);
  EXPECT_EQ(result.token_bucket_context->maxTokens(), 4); // double_entry_descriptor_.

  // single_entry_descriptor_ is also not consumed because there is no enough tokens.
  auto result2 = rate_limiter_->requestAllowed(descriptors);
  EXPECT_FALSE(result2.allowed);
  EXPECT_EQ(result2.token_bucket_context->maxTokens(), 5); // single_entry_descriptor_.

  // Now left 1 token for single_entry_descriptor_ and 1 token for double_entry_descriptor_.

  // 1 -> 3.5 tokens for single_entry_descriptor_.
  // 1 -> 4 tokens for double_entry_descriptor_.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptorStatus) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 2, 2);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(descriptor_);

  EXPECT_TRUE(rate_limit_result.allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(500));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  // 0 -> 1 tokens. 1500ms passed and 1 token will be added.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(3000));

  // 0 -> 2 tokens
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
}

// Verify null default token bucket.
TEST_F(LocalRateLimiterDescriptorImplTest, NullDefaultTokenBucket) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(0), 0, 0);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(descriptor_);

  EXPECT_TRUE(rate_limit_result.allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  // Not match any descriptor and default token bucket is null.
  auto no_match_result = rate_limiter_->requestAllowed(no_match_descriptor_);
  EXPECT_TRUE(no_match_result.allowed);
  EXPECT_FALSE(no_match_result.token_bucket_context);
}

// Verify is_negative refunds tokens back to the descriptor bucket, capped at max.
TEST_F(LocalRateLimiterDescriptorImplTest, IsNegativeRefundsTokens) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 5, 5, "2s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 8, 8);

  // First consume 2 tokens normally.
  std::vector<RateLimit::Descriptor> descriptors;
  descriptors.push_back(descriptor_[0]);
  descriptors[0].hits_addend_ = 2;

  auto result = rate_limiter_->requestAllowed(descriptors);
  EXPECT_TRUE(result.allowed);
  EXPECT_EQ(result.token_bucket_context->remainingTokens(), 3);

  // Now refund 1 token using is_negative.
  descriptors[0].hits_addend_ = 1;
  descriptors[0].is_negative_hits_ = true;

  result = rate_limiter_->requestAllowed(descriptors);
  EXPECT_TRUE(result.allowed);
  // 3 + 1 (refund) = 4 tokens remaining. No tokens consumed when is_negative_hits is set.
  EXPECT_EQ(result.token_bucket_context->remainingTokens(), 4);
}

// Verify is_negative refund is capped at max bucket size.
TEST_F(LocalRateLimiterDescriptorImplTest, IsNegativeCappedAtMax) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 3, 3, "2s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 8, 8);

  // Start with 3 tokens in the descriptor bucket.
  // Refill 10 with is_negative => should cap at max (3), so 3 tokens remaining.
  std::vector<RateLimit::Descriptor> descriptors;
  descriptors.push_back(descriptor_[0]);
  descriptors[0].hits_addend_ = 10;
  descriptors[0].is_negative_hits_ = true;

  auto result = rate_limiter_->requestAllowed(descriptors);
  EXPECT_TRUE(result.allowed);
  // Refill is capped at max bucket size (3), so tokens remain at 3.
  EXPECT_EQ(result.token_bucket_context->remainingTokens(), 3);
}

// Verify is_negative with hits_addend refills tokens without consuming.
TEST_F(LocalRateLimiterDescriptorImplTest, IsNegativeRefillsTokens) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 3, 3, "2s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 8, 8);

  // First consume 2 tokens normally to bring bucket to 1.
  std::vector<RateLimit::Descriptor> consume_descriptors;
  consume_descriptors.push_back(descriptor_[0]);
  consume_descriptors[0].hits_addend_ = 2;

  auto result = rate_limiter_->requestAllowed(consume_descriptors);
  EXPECT_TRUE(result.allowed);
  EXPECT_EQ(result.token_bucket_context->remainingTokens(), 1);

  // Now refill 1 token using is_negative. No tokens consumed.
  std::vector<RateLimit::Descriptor> refill_descriptors;
  refill_descriptors.push_back(descriptor_[0]);
  refill_descriptors[0].hits_addend_ = 1;
  refill_descriptors[0].is_negative_hits_ = true;

  result = rate_limiter_->requestAllowed(refill_descriptors);
  EXPECT_TRUE(result.allowed);
  // 1 + 1 (refill) = 2 tokens remaining.
  EXPECT_EQ(result.token_bucket_context->remainingTokens(), 2);
}

} // Namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
