#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

// ============================================================
// OrcaLoadReportHandler static method tests
// ============================================================

TEST(OrcaLoadReportHandlerTest, GetUtilizationFromOrcaReport_ApplicationUtilization) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_application_utilization(0.5);
  report.mutable_named_metrics()->insert({"foo", 0.3});
  report.set_cpu_utilization(0.6);
  EXPECT_EQ(OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, {"named_metrics.foo"}),
            0.5);
}

TEST(OrcaLoadReportHandlerTest, GetUtilizationFromOrcaReport_NamedMetrics) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.mutable_named_metrics()->insert({"foo", 0.3});
  report.set_cpu_utilization(0.6);
  EXPECT_EQ(OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, {"named_metrics.foo"}),
            0.3);
}

TEST(OrcaLoadReportHandlerTest, GetUtilizationFromOrcaReport_CpuUtilizationFallback) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.mutable_named_metrics()->insert({"bar", 0.3});
  report.set_cpu_utilization(0.6);
  // "named_metrics.foo" doesn't match "bar", so falls through to cpu_utilization.
  EXPECT_EQ(OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, {"named_metrics.foo"}),
            0.6);
}

TEST(OrcaLoadReportHandlerTest, GetUtilizationFromOrcaReport_NoUtilization) {
  xds::data::orca::v3::OrcaLoadReport report;
  EXPECT_EQ(OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, {"named_metrics.foo"}), 0);
}

TEST(OrcaLoadReportHandlerTest, CalculateWeightFromOrcaReport_Valid) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_rps_fractional(1000);
  report.set_application_utilization(0.5);
  auto result =
      OrcaLoadReportHandler::calculateWeightFromOrcaReport(report, {"named_metrics.foo"}, 0.0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 2000);
}

TEST(OrcaLoadReportHandlerTest, CalculateWeightFromOrcaReport_NoQps) {
  xds::data::orca::v3::OrcaLoadReport report;
  auto result =
      OrcaLoadReportHandler::calculateWeightFromOrcaReport(report, {"named_metrics.foo"}, 0.0);
  EXPECT_EQ(result.status(), absl::InvalidArgumentError("QPS must be positive"));
}

TEST(OrcaLoadReportHandlerTest, CalculateWeightFromOrcaReport_NoUtilization) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_rps_fractional(1000);
  auto result =
      OrcaLoadReportHandler::calculateWeightFromOrcaReport(report, {"named_metrics.foo"}, 0.0);
  EXPECT_EQ(result.status(), absl::InvalidArgumentError("Utilization must be positive"));
}

TEST(OrcaLoadReportHandlerTest, CalculateWeightFromOrcaReport_MaxWeight) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_rps_fractional(10000000000000L);
  report.set_application_utilization(0.0000001);
  auto result =
      OrcaLoadReportHandler::calculateWeightFromOrcaReport(report, {"named_metrics.foo"}, 0.0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), std::numeric_limits<uint32_t>::max());
}

TEST(OrcaLoadReportHandlerTest, CalculateWeightFromOrcaReport_ErrorPenalty) {
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_rps_fractional(1000);
  report.set_eps(100);
  report.set_application_utilization(0.5);
  auto result =
      OrcaLoadReportHandler::calculateWeightFromOrcaReport(report, {"named_metrics.foo"}, 2.0);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 1428);
}

// ============================================================
// OrcaHostLbPolicyData tests
// ============================================================

TEST(OrcaHostLbPolicyDataTest, GetWeightIfValid_Blackout) {
  OrcaHostLbPolicyData data(nullptr, 42,
                            /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
                            /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  // max_non_empty_since (2) < non_empty_since (5) → blackout period, return nullopt.
  EXPECT_FALSE(data.getWeightIfValid(MonotonicTime(std::chrono::seconds(2)),
                                     MonotonicTime(std::chrono::seconds(1))));
  // non_empty_since_ should not be reset during blackout.
  EXPECT_EQ(data.non_empty_since_.load(), MonotonicTime(std::chrono::seconds(5)));
}

TEST(OrcaHostLbPolicyDataTest, GetWeightIfValid_Expiration) {
  OrcaHostLbPolicyData data(nullptr, 42,
                            /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
                            /*last_update_time=*/MonotonicTime(std::chrono::seconds(7)));
  // last_update_time (7) < min_last_update_time (8) → expired, return nullopt.
  EXPECT_FALSE(data.getWeightIfValid(MonotonicTime(std::chrono::seconds(2)),
                                     MonotonicTime(std::chrono::seconds(8))));
  // Expiration resets non_empty_since_ to default.
  EXPECT_EQ(data.non_empty_since_.load(), OrcaHostLbPolicyData::kDefaultNonEmptySince);
}

TEST(OrcaHostLbPolicyDataTest, GetWeightIfValid_Valid) {
  OrcaHostLbPolicyData data(nullptr, 42,
                            /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
                            /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  // non_empty_since (1) <= max_non_empty_since (2) and
  // last_update_time (10) >= min_last_update_time (8) → valid.
  auto result = data.getWeightIfValid(MonotonicTime(std::chrono::seconds(2)),
                                      MonotonicTime(std::chrono::seconds(8)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 42);
}

TEST(OrcaHostLbPolicyDataTest, UpdateWeightNow_FirstUpdate) {
  OrcaHostLbPolicyData data(nullptr);
  EXPECT_EQ(data.non_empty_since_.load(), OrcaHostLbPolicyData::kDefaultNonEmptySince);
  EXPECT_EQ(data.weight_.load(), 1);

  MonotonicTime now(std::chrono::seconds(30));
  data.updateWeightNow(2000, now);
  EXPECT_EQ(data.weight_.load(), 2000);
  EXPECT_EQ(data.last_update_time_.load(), now);
  // First update sets non_empty_since_.
  EXPECT_EQ(data.non_empty_since_.load(), now);
}

TEST(OrcaHostLbPolicyDataTest, UpdateWeightNow_SubsequentUpdate) {
  MonotonicTime first_time(std::chrono::seconds(10));
  OrcaHostLbPolicyData data(nullptr, 100, first_time, first_time);

  MonotonicTime second_time(std::chrono::seconds(20));
  data.updateWeightNow(200, second_time);
  EXPECT_EQ(data.weight_.load(), 200);
  EXPECT_EQ(data.last_update_time_.load(), second_time);
  // non_empty_since_ should not be changed on subsequent update.
  EXPECT_EQ(data.non_empty_since_.load(), first_time);
}

TEST(OrcaHostLbPolicyDataTest, OnOrcaLoadReport_Success) {
  OrcaWeightManagerConfig config;
  config.metric_names_for_computing_utilization = {"named_metrics.foo"};
  config.error_utilization_penalty = 0.0;
  config.blackout_period = std::chrono::milliseconds(10000);
  config.weight_expiration_period = std::chrono::milliseconds(180000);
  config.weight_update_period = std::chrono::milliseconds(1000);

  Event::SimulatedTimeSystem time_system;
  time_system.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  auto handler = std::make_shared<OrcaLoadReportHandler>(config, time_system);
  OrcaHostLbPolicyData data(handler);

  xds::data::orca::v3::OrcaLoadReport report;
  report.set_rps_fractional(1000);
  report.set_application_utilization(0.5);

  Envoy::StreamInfo::MockStreamInfo mock_stream_info;
  EXPECT_EQ(data.onOrcaLoadReport(report, mock_stream_info), absl::OkStatus());
  EXPECT_EQ(data.weight_.load(), 2000);
  EXPECT_EQ(data.non_empty_since_.load(), MonotonicTime(std::chrono::seconds(30)));
  EXPECT_EQ(data.last_update_time_.load(), MonotonicTime(std::chrono::seconds(30)));
}

TEST(OrcaHostLbPolicyDataTest, OnOrcaLoadReport_ErrorPreservesState) {
  OrcaWeightManagerConfig config;
  config.metric_names_for_computing_utilization = {"named_metrics.foo"};
  config.error_utilization_penalty = 0.0;
  config.blackout_period = std::chrono::milliseconds(10000);
  config.weight_expiration_period = std::chrono::milliseconds(180000);
  config.weight_update_period = std::chrono::milliseconds(1000);

  Event::SimulatedTimeSystem time_system;
  time_system.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  auto handler = std::make_shared<OrcaLoadReportHandler>(config, time_system);
  OrcaHostLbPolicyData data(handler, 42,
                            /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
                            /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));

  xds::data::orca::v3::OrcaLoadReport report;
  // QPS is 0 → invalid report.
  report.set_rps_fractional(0);
  report.set_application_utilization(0.5);

  Envoy::StreamInfo::MockStreamInfo mock_stream_info;
  EXPECT_EQ(data.onOrcaLoadReport(report, mock_stream_info),
            absl::InvalidArgumentError("QPS must be positive"));
  // State should be preserved.
  EXPECT_EQ(data.weight_.load(), 42);
  EXPECT_EQ(data.non_empty_since_.load(), MonotonicTime(std::chrono::seconds(1)));
  EXPECT_EQ(data.last_update_time_.load(), MonotonicTime(std::chrono::seconds(10)));
}

// ============================================================
// OrcaWeightManager tests
// ============================================================

// Helper to create a MockHost that tracks weight and lb policy data state
// (since MOCK_METHOD doesn't store).
std::shared_ptr<NiceMock<Upstream::MockHost>>
makeWeightTrackingMockHost(uint32_t initial_weight = 1) {
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto weight = std::make_shared<uint32_t>(initial_weight);
  ON_CALL(*host, weight()).WillByDefault([weight]() -> uint32_t { return *weight; });
  ON_CALL(*host, weight(::testing::_)).WillByDefault([weight](uint32_t new_weight) {
    *weight = new_weight;
  });
  // Wire setLbPolicyData to actually store in lb_policy_data_.
  // Use raw pointer to avoid reference cycle (host → ON_CALL → lambda → host).
  auto* raw_host = host.get();
  ON_CALL(*host, setLbPolicyData(::testing::_))
      .WillByDefault(::testing::Invoke([raw_host](Upstream::HostLbPolicyDataPtr data) {
        raw_host->lb_policy_data_ = std::move(data);
      }));
  return host;
}

class OrcaWeightManagerTest : public testing::Test {
protected:
  void SetUp() override {
    config_.metric_names_for_computing_utilization = {"named_metrics.foo"};
    config_.error_utilization_penalty = 0.1;
    config_.blackout_period = std::chrono::milliseconds(10000);
    config_.weight_expiration_period = std::chrono::milliseconds(180000);
    config_.weight_update_period = std::chrono::milliseconds(1000);
  }

  OrcaWeightManagerConfig config_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::SimulatedTimeSystem time_system_;
  bool weights_updated_ = false;
};

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnHosts_AllValid) {
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  Upstream::HostVector hosts;
  for (int i = 0; i < 3; ++i) {
    auto host = makeWeightTrackingMockHost();
    host->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
        manager->reportHandler(), 40 + i,
        /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
        /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
    hosts.push_back(host);
  }

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  bool updated = manager->updateWeightsOnHosts(hosts);
  EXPECT_TRUE(updated);
  EXPECT_EQ(hosts[0]->weight(), 40);
  EXPECT_EQ(hosts[1]->weight(), 41);
  EXPECT_EQ(hosts[2]->weight(), 42);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnHosts_Mixed) {
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  Upstream::HostVector hosts;
  // First host has valid weight.
  auto h1 = makeWeightTrackingMockHost();
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 42,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  hosts.push_back(h1);

  // Other hosts have no data → default weight.
  for (int i = 0; i < 2; ++i) {
    hosts.push_back(makeWeightTrackingMockHost());
  }

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  bool updated = manager->updateWeightsOnHosts(hosts);
  EXPECT_TRUE(updated);
  EXPECT_EQ(hosts[0]->weight(), 42);
  // Default is median of [42] = 42.
  EXPECT_EQ(hosts[1]->weight(), 42);
  EXPECT_EQ(hosts[2]->weight(), 42);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnHosts_AllDefault) {
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  Upstream::HostVector hosts;
  for (int i = 0; i < 2; ++i) {
    hosts.push_back(makeWeightTrackingMockHost());
  }

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  bool updated = manager->updateWeightsOnHosts(hosts);
  // Default weight is 1, same as initial → no update.
  EXPECT_FALSE(updated);
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnHosts_EvenMedian) {
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  Upstream::HostVector hosts;
  auto h1 = makeWeightTrackingMockHost();
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 5,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  hosts.push_back(h1);

  auto h2 = makeWeightTrackingMockHost();
  h2->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 42,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  hosts.push_back(h2);

  // Third host has no data.
  hosts.push_back(makeWeightTrackingMockHost());

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  bool updated = manager->updateWeightsOnHosts(hosts);
  EXPECT_TRUE(updated);
  EXPECT_EQ(hosts[0]->weight(), 5);
  EXPECT_EQ(hosts[1]->weight(), 42);
  // Even median of [5, 42] = (5+42)/2 = 23.
  EXPECT_EQ(hosts[2]->weight(), 23);
}

TEST_F(OrcaWeightManagerTest, GetWeightIfValidFromHost_NoData) {
  NiceMock<Upstream::MockHost> host;
  EXPECT_FALSE(OrcaWeightManager::getWeightIfValidFromHost(host, MonotonicTime::min(),
                                                           MonotonicTime::max()));
}

TEST_F(OrcaWeightManagerTest, GetWeightIfValidFromHost_Valid) {
  NiceMock<Upstream::MockHost> host;
  host.lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      nullptr, 42,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  auto result = OrcaWeightManager::getWeightIfValidFromHost(
      host, MonotonicTime(std::chrono::seconds(2)), MonotonicTime(std::chrono::seconds(8)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 42);
}

// ============================================================
// OrcaWeightManager lifecycle tests (initialize, timer, callbacks)
// ============================================================

TEST_F(OrcaWeightManagerTest, Initialize_AttachesHostDataToExistingHosts) {
  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector hosts;
  for (int i = 0; i < 3; ++i) {
    hosts.push_back(makeWeightTrackingMockHost());
  }
  host_set->hosts_ = hosts;

  // Verify no host has LB data before initialize.
  for (const auto& host : hosts) {
    EXPECT_FALSE(host->lbPolicyData().has_value());
  }

  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  auto status = manager->initialize();
  EXPECT_TRUE(status.ok());

  // Verify all hosts now have LB data attached.
  for (const auto& host : hosts) {
    EXPECT_TRUE(host->lbPolicyData().has_value());
    auto typed = host->typedLbPolicyData<OrcaHostLbPolicyData>();
    EXPECT_TRUE(typed.has_value());
  }
}

TEST_F(OrcaWeightManagerTest, Initialize_StartsWeightCalculationTimer) {
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  auto status = manager->initialize();
  EXPECT_TRUE(status.ok());
}

TEST_F(OrcaWeightManagerTest, Initialize_PriorityUpdateCallbackAttachesDataToNewHosts) {
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  auto status = manager->initialize();
  EXPECT_TRUE(status.ok());

  // Add new hosts via priority update callback.
  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector new_hosts;
  for (int i = 0; i < 2; ++i) {
    new_hosts.push_back(makeWeightTrackingMockHost());
  }
  host_set->hosts_ = new_hosts;

  // Trigger priority update callback with new hosts.
  host_set->runCallbacks(new_hosts, {});

  // Verify new hosts have LB data attached.
  for (const auto& host : new_hosts) {
    EXPECT_TRUE(host->lbPolicyData().has_value());
  }
}

TEST_F(OrcaWeightManagerTest, TimerCallback_UpdatesWeightsAndReenablesTimer) {
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  auto status = manager->initialize();
  EXPECT_TRUE(status.ok());

  // Set up hosts with valid weights so the callback fires on change.
  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector hosts;
  auto h1 = makeWeightTrackingMockHost();
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 100,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(50)));
  hosts.push_back(h1);
  host_set->hosts_ = hosts;

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(60)));

  // Timer callback should: update weights, then re-enable timer.
  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  timer->invokeCallback();

  EXPECT_TRUE(weights_updated_);
  EXPECT_EQ(hosts[0]->weight(), 100);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnMainThread_CallbackFiredOnChange) {
  new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  // Set up hosts with valid weights that differ from current weight.
  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector hosts;
  auto h1 = makeWeightTrackingMockHost(/*initial_weight=*/1);
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 200,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(50)));
  hosts.push_back(h1);
  host_set->hosts_ = hosts;

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(60)));

  EXPECT_FALSE(weights_updated_);
  manager->updateWeightsOnMainThread();
  EXPECT_TRUE(weights_updated_);
  EXPECT_EQ(hosts[0]->weight(), 200);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnMainThread_NoCallbackWhenNoChange) {
  new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  // Hosts with no data — default weight is 1, same as initial.
  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector hosts;
  hosts.push_back(makeWeightTrackingMockHost(/*initial_weight=*/1));
  host_set->hosts_ = hosts;

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(60)));

  manager->updateWeightsOnMainThread();
  // Default weight (1) equals initial weight (1), so no change → no callback.
  EXPECT_FALSE(weights_updated_);
}

TEST_F(OrcaWeightManagerTest, UpdateWeightsOnMainThread_MultiplePriorities) {
  new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  // Priority 0: host with valid weight.
  auto* host_set0 = priority_set_.getMockHostSet(0);
  auto h0 = makeWeightTrackingMockHost(/*initial_weight=*/1);
  h0->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 50,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(50)));
  host_set0->hosts_ = {h0};

  // Priority 1: host with valid weight.
  auto* host_set1 = priority_set_.getMockHostSet(1);
  auto h1 = makeWeightTrackingMockHost(/*initial_weight=*/1);
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 75,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(50)));
  host_set1->hosts_ = {h1};

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(60)));

  manager->updateWeightsOnMainThread();
  EXPECT_TRUE(weights_updated_);
  EXPECT_EQ(h0->weight(), 50);
  EXPECT_EQ(h1->weight(), 75);
}

TEST_F(OrcaWeightManagerTest, AddLbPolicyDataToHosts_SkipsHostsWithExistingData) {
  auto* timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  auto* host_set = priority_set_.getMockHostSet(0);
  Upstream::HostVector hosts;

  // Host with existing data.
  auto h1 = makeWeightTrackingMockHost();
  h1->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
      manager->reportHandler(), 42,
      /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
      /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  hosts.push_back(h1);

  // Host without data.
  auto h2 = makeWeightTrackingMockHost();
  hosts.push_back(h2);

  host_set->hosts_ = hosts;

  EXPECT_CALL(*timer, enableTimer(config_.weight_update_period, nullptr));
  auto status = manager->initialize();
  EXPECT_TRUE(status.ok());

  // h1's existing data should be preserved (weight=42).
  auto typed_h1 = h1->typedLbPolicyData<OrcaHostLbPolicyData>();
  ASSERT_TRUE(typed_h1.has_value());
  EXPECT_EQ(typed_h1->weight_.load(), 42);

  // h2 should now have fresh data (default weight=1).
  auto typed_h2 = h2->typedLbPolicyData<OrcaHostLbPolicyData>();
  ASSERT_TRUE(typed_h2.has_value());
  EXPECT_EQ(typed_h2->weight_.load(), 1);
}

TEST_F(OrcaWeightManagerTest, OddMedian) {
  new NiceMock<Event::MockTimer>(&dispatcher_);
  auto manager = std::make_unique<OrcaWeightManager>(
      config_, priority_set_, time_system_, dispatcher_, [this]() { weights_updated_ = true; });

  Upstream::HostVector hosts;
  // 3 hosts with valid weights: 10, 20, 30 → median = 20.
  for (uint32_t w : {10u, 20u, 30u}) {
    auto h = makeWeightTrackingMockHost();
    h->lb_policy_data_ = std::make_unique<OrcaHostLbPolicyData>(
        manager->reportHandler(), w,
        /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
        /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
    hosts.push_back(h);
  }
  // 1 host with no data → gets median default.
  hosts.push_back(makeWeightTrackingMockHost());

  time_system_.setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  bool updated = manager->updateWeightsOnHosts(hosts);
  EXPECT_TRUE(updated);
  EXPECT_EQ(hosts[0]->weight(), 10);
  EXPECT_EQ(hosts[1]->weight(), 20);
  EXPECT_EQ(hosts[2]->weight(), 30);
  EXPECT_EQ(hosts[3]->weight(), 20); // Odd median of [10, 20, 30].
}

} // namespace
} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
