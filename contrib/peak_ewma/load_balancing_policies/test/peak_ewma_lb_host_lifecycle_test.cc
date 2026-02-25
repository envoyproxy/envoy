#include "source/common/network/address_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"

#include "contrib/peak_ewma/load_balancing_policies/source/host_data.h"
#include "contrib/peak_ewma/load_balancing_policies/source/peak_ewma_lb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

class PeakEwmaHostLifecycleTest : public ::testing::Test {
protected:
  void SetUp() override {
    stat_names_ = std::make_unique<Upstream::ClusterLbStatNames>(store_.symbolTable());
    stats_ = std::make_unique<Upstream::ClusterLbStats>(*stat_names_, *store_.rootScope());

    for (int i = 0; i < 3; ++i) {
      auto address = std::make_shared<Network::Address::Ipv4Instance>(
          "10.0.0." + std::to_string(i + 1), 8080 + i);
      auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
      ON_CALL(*host, address()).WillByDefault(Return(address));
      ON_CALL(*host, setLbPolicyData(_))
          .WillByDefault(Invoke([raw = host.get()](Upstream::HostLbPolicyDataPtr data) {
            raw->lb_policy_data_ = std::move(data);
          }));
      ON_CALL(*host, lbPolicyData()).WillByDefault(Invoke([raw = host.get()]() {
        if (raw->lb_policy_data_) {
          return OptRef<Upstream::HostLbPolicyData>(*raw->lb_policy_data_);
        }
        return OptRef<Upstream::HostLbPolicyData>();
      }));
      hosts_.push_back(host);
    }

    host_set_ = priority_set_.getMockHostSet(0);
    host_set_->hosts_ = hosts_;
    host_set_->healthy_hosts_ = hosts_;

    ON_CALL(priority_set_, hostSetsPerPriority())
        .WillByDefault(ReturnRef(priority_set_.host_sets_));
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(*store_.rootScope()));
    ON_CALL(time_source_, monotonicTime())
        .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1000000))));

    config_.mutable_decay_time()->set_seconds(10);
    config_.mutable_aggregation_interval()->set_nanos(100000000); // 100ms
  }

  void createLoadBalancer() {
    lb_ = std::make_unique<PeakEwmaLoadBalancer>(priority_set_, nullptr, *stats_, runtime_, random_,
                                                 50, *cluster_info_, time_source_, config_);
  }

  Stats::TestUtil::TestStore store_;
  std::unique_ptr<Upstream::ClusterLbStatNames> stat_names_;
  std::unique_ptr<Upstream::ClusterLbStats> stats_;

  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::MockHostSet* host_set_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockTimeSystem> time_source_;

  std::vector<Upstream::HostSharedPtr> hosts_;
  std::unique_ptr<PeakEwmaLoadBalancer> lb_;
  envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma config_;
};

// ============================================================================
// Bug regression tests — assert CORRECT (fixed) behavior.
// These FAILED against the buggy code, demonstrating the bugs.
// With fixes applied, they PASS.
// ============================================================================

// BUG 4 regression: Destructor must NOT clear host lbPolicyData.
// Previously, the destructor iterated hosts and called setLbPolicyData(nullptr),
// which could race with workers still reading the data. Now the destructor
// leaves host data alone — cleanup happens naturally via Host shared_ptr lifecycle.
TEST_F(PeakEwmaHostLifecycleTest, DestructorPreservesHostPolicyData) {
  createLoadBalancer();

  for (const auto& host : hosts_) {
    EXPECT_TRUE(host->lbPolicyData().has_value())
        << "Host should have lbPolicyData after LB creation";
  }

  lb_.reset();

  // After fix: host data must still be present (not cleared by destructor).
  for (const auto& host : hosts_) {
    EXPECT_TRUE(host->lbPolicyData().has_value())
        << "Host lbPolicyData should persist after LB destruction";
  }
}

// BUG 3 regression: Host removal must clean up all_host_stats_ entries.
// Previously, all_host_stats_ was never cleaned on host removal, leaking a
// shared_ptr to the removed host. Now the priority_update_cb_ erases entries
// for removed hosts.
TEST_F(PeakEwmaHostLifecycleTest, HostRemovalCleansUpStats) {
  createLoadBalancer();

  // Advance time past aggregation interval so chooseHost triggers aggregation,
  // which populates all_host_stats_ (holds HostConstSharedPtr keys).
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1000200))));
  lb_->chooseHost(nullptr);

  // Remove host 0 from the priority set. Use a scope block so removed_hosts
  // doesn't inflate the use_count at assertion time.
  {
    Upstream::HostVector removed_hosts = {hosts_[0]};
    host_set_->hosts_ = {hosts_[1], hosts_[2]};
    host_set_->healthy_hosts_ = {hosts_[1], hosts_[2]};
    host_set_->runCallbacks({}, removed_hosts);
  }

  // After fix: all_host_stats_ should have erased the removed host's entry,
  // releasing its shared_ptr. Only our test's hosts_ vector should hold a ref.
  EXPECT_EQ(hosts_[0].use_count(), 1)
      << "LB should release shared_ptr reference to removed host (all_host_stats_ cleanup)";
}

// BUG 1 regression: Destruction must not rely on dispatcher_.post() for timer cleanup.
// Previously, the destructor moved the timer into a post() callback. If the post
// was never executed (NiceMock, shutdown race), the timer callback retained a
// dangling `this` pointer — a use-after-free. Now there is no timer at all;
// aggregation happens inline in chooseHost(). Destruction is trivially safe.
TEST_F(PeakEwmaHostLifecycleTest, DestructorDoesNotCrash) {
  createLoadBalancer();

  // Exercise the LB so internal state is populated.
  lb_->chooseHost(nullptr);

  // Destroy should be trivially safe — no timer, no post(), no race.
  lb_.reset();
}

// ============================================================================
// Coverage tests
// ============================================================================

// Coverage: hosts added via priority update callback get policy data attached.
TEST_F(PeakEwmaHostLifecycleTest, HostAddedViaCallbackGetsPolicyData) {
  createLoadBalancer();

  auto address = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.100", 9090);
  auto new_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*new_host, address()).WillByDefault(Return(address));
  ON_CALL(*new_host, setLbPolicyData(_))
      .WillByDefault(Invoke([raw = new_host.get()](Upstream::HostLbPolicyDataPtr data) {
        raw->lb_policy_data_ = std::move(data);
      }));
  ON_CALL(*new_host, lbPolicyData()).WillByDefault(Invoke([raw = new_host.get()]() {
    if (raw->lb_policy_data_) {
      return OptRef<Upstream::HostLbPolicyData>(*raw->lb_policy_data_);
    }
    return OptRef<Upstream::HostLbPolicyData>();
  }));

  Upstream::HostVector added_hosts = {new_host};
  host_set_->hosts_.push_back(new_host);
  host_set_->healthy_hosts_.push_back(new_host);
  host_set_->runCallbacks(added_hosts, {});

  EXPECT_TRUE(new_host->lbPolicyData().has_value()) << "Newly added host should have lbPolicyData";
}

// Coverage: chooseHost works after removing a host.
TEST_F(PeakEwmaHostLifecycleTest, ChooseHostAfterHostRemoval) {
  createLoadBalancer();

  Upstream::HostVector removed = {hosts_[0]};
  host_set_->hosts_ = {hosts_[1], hosts_[2]};
  host_set_->healthy_hosts_ = {hosts_[1], hosts_[2]};
  host_set_->runCallbacks({}, removed);

  for (int i = 0; i < 10; ++i) {
    auto result = lb_->chooseHost(nullptr);
    EXPECT_NE(result.host, nullptr);
    EXPECT_NE(result.host, hosts_[0]) << "Removed host should not be selected";
  }
}

// ============================================================================
// Ring buffer overflow and alpha calculation regression tests.
// ============================================================================

// Regression: When more than max_samples are written between aggregations,
// processHostSamples must skip overwritten slots instead of re-reading them.
TEST_F(PeakEwmaHostLifecycleTest, RingBufferOverflowSkipsOverwrittenSamples) {
  // Use a small ring buffer to make overflow easy to trigger.
  config_.mutable_max_samples_per_host()->set_value(10);
  config_.mutable_decay_time()->set_seconds(1);
  createLoadBalancer();

  auto* data = dynamic_cast<PeakEwmaHostLbPolicyData*>(hosts_[0]->lbPolicyData().ptr());
  ASSERT_NE(data, nullptr);

  // Write 15 samples (overflow by 5). First 5 slots get overwritten.
  // Write old samples (RTT=1000ms) first, then newer samples (RTT=10ms).
  uint64_t base_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              MonotonicTime(std::chrono::milliseconds(1000000)).time_since_epoch())
                              .count();

  for (int i = 0; i < 15; ++i) {
    double rtt = (i < 5) ? 1000.0 : 10.0;
    data->recordRttSample(rtt, base_time_ns + i * 1000000); // 1ms apart
  }

  // Advance time past aggregation interval.
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1000200))));
  lb_->chooseHost(nullptr);

  // Only the 10 most recent samples should be processed. Those are all RTT=10ms
  // (samples 5-14). If the bug is present, some slots would be read twice,
  // pulling in stale RTT=1000ms values and inflating the EWMA.
  double ewma = data->getEwmaRtt();
  EXPECT_GT(ewma, 0.0);
  EXPECT_LE(ewma, 15.0) << "EWMA should reflect only the valid 10ms samples, not stale 1000ms "
                           "data from overwritten slots. Got: "
                        << ewma;
}

// Regression: Overflow by exactly one past max_samples still produces sane EWMA.
TEST_F(PeakEwmaHostLifecycleTest, RingBufferOverflowExactlyOnePastMax) {
  config_.mutable_max_samples_per_host()->set_value(10);
  config_.mutable_decay_time()->set_seconds(1);
  createLoadBalancer();

  auto* data = dynamic_cast<PeakEwmaHostLbPolicyData*>(hosts_[0]->lbPolicyData().ptr());
  ASSERT_NE(data, nullptr);

  uint64_t base_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              MonotonicTime(std::chrono::milliseconds(1000000)).time_since_epoch())
                              .count();

  // Write 11 samples (one past max). Slot 0 gets overwritten.
  // First sample: RTT=1000ms (will be overwritten). Rest: RTT=20ms.
  data->recordRttSample(1000.0, base_time_ns);
  for (int i = 1; i <= 10; ++i) {
    data->recordRttSample(20.0, base_time_ns + i * 1000000);
  }

  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1000200))));
  lb_->chooseHost(nullptr);

  double ewma = data->getEwmaRtt();
  EXPECT_GT(ewma, 0.0);
  EXPECT_LE(ewma, 25.0) << "EWMA should reflect the 10 valid 20ms samples. Got: " << ewma;
}

// Regression: Newer samples must have more influence on EWMA than older ones.
// With the bug, alpha was computed as time_from_aggregation - sample_time, making
// older samples get MORE weight. The fix computes alpha as sample_time - previous_update_time.
TEST_F(PeakEwmaHostLifecycleTest, NewerSamplesHaveMoreInfluenceOnEwma) {
  config_.mutable_decay_time()->set_seconds(1); // tau = 1s
  createLoadBalancer();

  auto* data = dynamic_cast<PeakEwmaHostLbPolicyData*>(hosts_[0]->lbPolicyData().ptr());
  ASSERT_NE(data, nullptr);

  // Record old sample: RTT=500ms at T=2s.
  uint64_t t_2s = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      MonotonicTime(std::chrono::seconds(2)).time_since_epoch())
                      .count();
  data->recordRttSample(500.0, t_2s);

  // Record newer sample: RTT=10ms at T=4s.
  uint64_t t_4s = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      MonotonicTime(std::chrono::seconds(4)).time_since_epoch())
                      .count();
  data->recordRttSample(10.0, t_4s);

  // Aggregate at T=5s.
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::seconds(5))));
  lb_->chooseHost(nullptr);

  // With correct alpha ordering (newer samples weighted more):
  // First sample initializes EWMA=500. Second sample (2s gap, tau=1s) has
  // alpha = 1 - e^(-2) ≈ 0.86, so EWMA ≈ 0.86*10 + 0.14*500 ≈ 78.6.
  // With the bug (older samples weighted more): alpha for the newer sample
  // would be based on (5s - 4s) = 1s gap giving alpha ≈ 0.63, while the older
  // sample gets alpha based on (5s - 2s) = 3s giving alpha ≈ 0.95.
  // The EWMA would be much closer to 500.
  double ewma = data->getEwmaRtt();
  EXPECT_LT(ewma, 100.0) << "EWMA should be much closer to the newer 10ms sample than the older "
                            "500ms sample. Got: "
                         << ewma;
}

// Regression: Alpha must be based on time since last EWMA update, not aggregation time.
// With the bug, delaying aggregation inflates alpha, making a single new sample
// dominate the EWMA even if it arrived shortly after the previous update.
TEST_F(PeakEwmaHostLifecycleTest, AlphaUsesLastUpdateTimestampNotAggregationTime) {
  config_.mutable_decay_time()->set_seconds(1); // tau = 1s
  createLoadBalancer();

  auto* data = dynamic_cast<PeakEwmaHostLbPolicyData*>(hosts_[0]->lbPolicyData().ptr());
  ASSERT_NE(data, nullptr);

  // LB was created at T=1000s (SetUp default). Use times after that.
  // Record sample A: RTT=100ms at T=1001s.
  uint64_t t_1001s = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         MonotonicTime(std::chrono::seconds(1001)).time_since_epoch())
                         .count();
  data->recordRttSample(100.0, t_1001s);

  // Aggregate at T=1001.1s (past the 100ms aggregation interval) — establishes EWMA=100.
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1001100))));
  lb_->chooseHost(nullptr);
  EXPECT_NEAR(data->getEwmaRtt(), 100.0, 1.0);

  // Record sample B: RTT=50ms at T=1001.2s (200ms after sample A).
  uint64_t t_1001_2s = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           MonotonicTime(std::chrono::milliseconds(1001200)).time_since_epoch())
                           .count();
  data->recordRttSample(50.0, t_1001_2s);

  // Delay aggregation until T=1010s (9 seconds after first aggregation).
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::seconds(1010))));
  lb_->chooseHost(nullptr);

  // With the fix: alpha is based on (1001.2s - 1001s) = 200ms gap → alpha ≈ 0.18
  // EWMA ≈ 0.18*50 + 0.82*100 ≈ 91.
  // With the bug: alpha is based on (1010s - 1001.2s) = 8.8s gap → alpha ≈ 1.0
  // EWMA ≈ 1.0*50 + 0.0*100 ≈ 50.
  double ewma = data->getEwmaRtt();
  EXPECT_GT(ewma, 90.0) << "EWMA should barely change because sample B arrived only 200ms after "
                           "sample A, regardless of aggregation delay. Got: "
                        << ewma;
}

// Coverage: inline aggregation triggers in chooseHost when interval elapses.
TEST_F(PeakEwmaHostLifecycleTest, AggregationHappensInlineOnChooseHost) {
  createLoadBalancer();

  // Record an RTT sample on host 0.
  auto* data = dynamic_cast<PeakEwmaHostLbPolicyData*>(hosts_[0]->lbPolicyData().ptr());
  ASSERT_NE(data, nullptr);
  uint64_t sample_time_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          MonotonicTime(std::chrono::milliseconds(1000050)).time_since_epoch())
          .count();
  data->recordRttSample(5.0, sample_time_ns);

  // EWMA should still be 0 before aggregation.
  EXPECT_DOUBLE_EQ(data->getEwmaRtt(), 0.0);

  // Advance time past the aggregation interval (100ms).
  ON_CALL(time_source_, monotonicTime())
      .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1000200))));

  // chooseHost should trigger inline aggregation.
  lb_->chooseHost(nullptr);

  // After aggregation, the EWMA should be updated with the sample.
  EXPECT_GT(data->getEwmaRtt(), 0.0) << "EWMA should be updated after inline aggregation";
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
