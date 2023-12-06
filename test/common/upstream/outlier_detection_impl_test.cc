#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/outlier_detection.pb.h"
#include "envoy/data/cluster/v3/outlier_detection_event.pb.h"

#include "source/common/network/utility.h"
#include "source/common/upstream/outlier_detection_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace Outlier {
namespace {

TEST(OutlierDetectorImplFactoryTest, NoDetector) {
  NiceMock<MockClusterMockPrioritySet> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Random::MockRandomGenerator> random;
  EXPECT_EQ(nullptr,
            DetectorImplFactory::createForCluster(cluster, defaultStaticCluster("fake_cluster"),
                                                  dispatcher, runtime, nullptr, random));
}

TEST(OutlierDetectorImplFactoryTest, Detector) {
  auto fake_cluster = defaultStaticCluster("fake_cluster");
  fake_cluster.mutable_outlier_detection();

  NiceMock<MockClusterMockPrioritySet> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Random::MockRandomGenerator> random;
  EXPECT_NE(nullptr, DetectorImplFactory::createForCluster(cluster, fake_cluster, dispatcher,
                                                           runtime, nullptr, random));
}

class CallbackChecker {
public:
  MOCK_METHOD(void, check, (HostSharedPtr host));
};

class OutlierDetectorImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  OutlierDetectorImplTest()
      : outlier_detection_ejections_active_(cluster_.info_->stats_store_.gauge(
            "outlier_detection.ejections_active", Stats::Gauge::ImportMode::Accumulate)) {
    ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingSuccessRateRuntime, 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveLocalOriginFailureRuntime, 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingLocalOriginSuccessRateRuntime, 100))
        .WillByDefault(Return(true));

    // Prepare separate config with split_external_local_origin_errors set to true.
    // It will be used for tests with split external and local origin errors.
    outlier_detection_split_.set_split_external_local_origin_errors(true);
  }

  void addHosts(std::vector<std::string> urls, bool primary = true) {
    HostVector& hosts = primary ? hosts_ : failover_hosts_;
    for (auto& url : urls) {
      hosts.emplace_back(makeTestHost(cluster_.info_, url, simTime()));
    }
  }

  template <typename T> void loadRq(HostVector& hosts, int num_rq, T code) {
    for (auto& host : hosts) {
      loadRq(host, num_rq, code);
    }
  }

  void loadRq(HostSharedPtr host, int num_rq, int http_code) {
    for (int i = 0; i < num_rq; i++) {
      host->outlierDetector().putHttpResponseCode(http_code);
    }
  }

  void loadRq(HostSharedPtr host, int num_rq, Result result) {
    for (int i = 0; i < num_rq; i++) {
      host->outlierDetector().putResult(result);
    }
  }

  NiceMock<MockClusterMockPrioritySet> cluster_;
  HostVector& hosts_ = cluster_.prioritySet().getMockHostSet(0)->hosts_;
  HostVector& failover_hosts_ = cluster_.prioritySet().getMockHostSet(1)->hosts_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Event::MockTimer* interval_timer_ = new Event::MockTimer(&dispatcher_);
  CallbackChecker checker_;
  Event::SimulatedTimeSystem time_system_;
  std::shared_ptr<MockEventLogger> event_logger_{new MockEventLogger()};
  envoy::config::cluster::v3::OutlierDetection empty_outlier_detection_;
  envoy::config::cluster::v3::OutlierDetection outlier_detection_split_;
  Stats::Gauge& outlier_detection_ejections_active_;
};

TEST_F(OutlierDetectorImplTest, DetectorStaticConfig) {
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 10s
consecutive_5xx: 10
max_ejection_percent: 50
enforcing_consecutive_5xx: 10
enforcing_success_rate: 20
success_rate_minimum_hosts: 50
success_rate_request_volume: 200
success_rate_stdev_factor: 3000
failure_percentage_minimum_hosts: 10
failure_percentage_request_volume: 25
failure_percentage_threshold: 70
max_ejection_time: 400s
  )EOF";

  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(10000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(10UL, detector->config().consecutive5xx());
  EXPECT_EQ(5UL, detector->config().consecutiveGatewayFailure());
  EXPECT_EQ(50UL, detector->config().maxEjectionPercent());
  EXPECT_EQ(10UL, detector->config().enforcingConsecutive5xx());
  EXPECT_EQ(0UL, detector->config().enforcingConsecutiveGatewayFailure());
  EXPECT_EQ(20UL, detector->config().enforcingSuccessRate());
  EXPECT_EQ(50UL, detector->config().successRateMinimumHosts());
  EXPECT_EQ(200UL, detector->config().successRateRequestVolume());
  EXPECT_EQ(3000UL, detector->config().successRateStdevFactor());
  EXPECT_EQ(0UL, detector->config().enforcingFailurePercentage());
  EXPECT_EQ(0UL, detector->config().enforcingFailurePercentageLocalOrigin());
  EXPECT_EQ(10UL, detector->config().failurePercentageMinimumHosts());
  EXPECT_EQ(25UL, detector->config().failurePercentageRequestVolume());
  EXPECT_EQ(70UL, detector->config().failurePercentageThreshold());
  EXPECT_EQ(400000UL, detector->config().maxEjectionTimeMs());
}

// Test verifies that detector is properly initialized with
// default max_ejection_time when such value is
// not specified in config.
TEST_F(OutlierDetectorImplTest, DetectorStaticConfigMaxDefaultValue) {
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 10s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(10000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(300000UL, detector->config().maxEjectionTimeMs());
}

// Test verifies that invalid outlier detector's config is rejected.
TEST_F(OutlierDetectorImplTest, DetectorStaticConfigiInvalidMaxEjectTime) {
  // Create invalid config. max_ejection_time must not be smaller than base_ejection_time.
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 10s
consecutive_5xx: 10
max_ejection_percent: 50
enforcing_consecutive_5xx: 10
enforcing_success_rate: 20
success_rate_minimum_hosts: 50
success_rate_request_volume: 200
success_rate_stdev_factor: 3000
failure_percentage_minimum_hosts: 10
failure_percentage_request_volume: 25
failure_percentage_threshold: 70
max_ejection_time: 3s
  )EOF";

  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  // Detector should reject the config.
  ASSERT_THROW(DetectorImpl::create(cluster_, outlier_detection, dispatcher_, runtime_,
                                    time_system_, event_logger_, random_),
               EnvoyException);
}

// Test verifies that legacy config without max_ejection_time value
// specified and base_ejection_time value larger than default value
// of max_ejection_time will be accepted.
// Values of base_ejection_time and max_ejection_time will be equal.
TEST_F(OutlierDetectorImplTest, DetectorStaticConfigBaseLargerThanMaxNoMax) {
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 400s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(400000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(400000UL, detector->config().maxEjectionTimeMs());
}

TEST_F(OutlierDetectorImplTest, DestroyWithActive) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"}, true);
  addHosts({"tcp://127.0.0.1:81"}, false);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);
  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  loadRq(failover_hosts_[0], 4, 500);
  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(failover_hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(failover_hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(failover_hosts_[0], 1, 500);
  EXPECT_TRUE(failover_hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(2UL, outlier_detection_ejections_active_.value());

  detector.reset();
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, DestroyHostInUse) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  detector.reset();

  loadRq(hosts_[0], 5, 500);
}

/*
 Tests scenario when connect errors are reported by Non-http codes and success is reported by
 http codes. (this happens in http router).
*/
TEST_F(OutlierDetectorImplTest, BasicFlow5xxViaHttpCodes) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error on host[0] by reporting HTTP codes.
  loadRq(hosts_[0], 1, 500);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(30001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      2UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_consecutive_gateway_failure")
                     .value());
}

/*
 Tests scenario when active health check clears the FAILED_OUTLIER_CHECK flag.
*/
TEST_F(OutlierDetectorImplTest, BasicFlow5xxViaHttpCodesWithActiveHCUnejectHost) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));

  // Add health checker to cluster and validate that host call back is called.
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  ON_CALL(cluster_, healthChecker()).WillByDefault(Return(health_checker.get()));

  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error on host[0] by reporting HTTP codes.
  loadRq(hosts_[0], 1, 500);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  // Trigger the health checker callbacks with "Changed" status and validate host is unejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  health_checker->runCallbacks(hosts_[0], HealthTransition::Changed);
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause a consecutive 5xx error again on host[0] by reporting HTTP codes.
  loadRq(hosts_[0], 1, 500);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  // Trigger the health checker callbacks with "UnChanged" status and validate host is unejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  health_checker->runCallbacks(hosts_[0], HealthTransition::Unchanged);
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(3UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      3UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_consecutive_gateway_failure")
                     .value());
}

/*
 Tests scenario when active health check is disabled from clearing the FAILED_OUTLIER_CHECK flag.
*/
TEST_F(OutlierDetectorImplTest, BasicFlow5xxViaHttpCodesWithActiveHCDoNotUnejectHost) {
  const std::string yaml = R"EOF(
successful_active_health_check_uneject_host: false
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));

  // Add health checker to cluster and validate that host call back is not called.
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  // The host check complete callback is not triggered.
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(0);
  ON_CALL(cluster_, healthChecker()).WillByDefault(Return(health_checker.get()));

  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error on host[0] by reporting HTTP codes.
  loadRq(hosts_[0], 1, 500);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  // The health checker callbacks won't be triggered with "Changed" status. The host is unejected.
  EXPECT_CALL(checker_, check(hosts_[0])).Times(0);
  // Uneject doesn't happen
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])))
      .Times(0);
  health_checker->runCallbacks(hosts_[0], HealthTransition::Changed);
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      1UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_consecutive_gateway_failure")
                     .value());
}

/* Test verifies the LOCAL_ORIGIN_CONNECT_SUCCESS with optional HTTP code 200,
   cancels LOCAL_ORIGIN_CONNECT_FAILED event.
*/
TEST_F(OutlierDetectorImplTest, ConnectSuccessWithOptionalHTTP_OK) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Make sure that in non-split mode LOCAL_ORIGIN_CONNECT_SUCCESS with optional HTTP code 200
  // cancels LOCAL_ORIGIN_CONNECT_FAILED.
  // such scenario is used by tcp_proxy.
  for (auto i = 0; i < 100; i++) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectSuccess,
                                           absl::optional<uint64_t>(enumToInt(Http::Code::OK)));
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  }
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

/* Test verifies the EXT_ORIGIN_REQUEST_SUCCESS cancels EXT_ORIGIN_REQUEST_FAILED event in non-split
 * mode.
 * EXT_ORIGIN_REQUEST_FAILED is mapped to 5xx code and EXT_ORIGIN_REQUEST_SUCCESS is mapped to 200
 * code.
 */
TEST_F(OutlierDetectorImplTest, ExternalOriginEventsNonSplit) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(60));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  addHosts({"tcp://127.0.0.2:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Make sure that EXT_ORIGIN_REQUEST_SUCCESS cancels EXT_ORIGIN_REQUEST_FAILED
  // such scenario is used by redis filter.
  for (auto i = 0; i < 100; i++) {
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestFailed);
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestSuccess);
  }
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Now make sure that EXT_ORIGIN_REQUEST_FAILED ejects the host
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  for (auto i = 0; i < 100; i++) {
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestFailed);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

TEST_F(OutlierDetectorImplTest, BasicFlow5xxViaNonHttpCodes) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error on host[0] by reporting Non-HTTP codes.
  loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(30001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      2UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_consecutive_gateway_failure")
                     .value());
}

/**
 * Test that the consecutive gateway failure detector correctly fires, and also successfully
 * retriggers after uneject. This will also ensure that the stats counters end up with the expected
 * values.
 */
TEST_F(OutlierDetectorImplTest, BasicFlowGatewayFailure) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));

  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 0))
      .WillByDefault(Return(true));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(false));

  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 1, 503);
  loadRq(hosts_[0], 1, 500);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 2, 503);
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false));
  loadRq(hosts_[0], 2, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(30001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  // Check preserves deprecated counter behaviour
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      2UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_enforced_total").value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_gateway_failure")
                     .value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_gateway_failure")
                     .value());

  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_5xx")
                     .value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_5xx")
                     .value());
}

/*
 * Test passing of optional HTTP code with Result:: LOCAL_ORIGIN_TIMEOUT
 */
TEST_F(OutlierDetectorImplTest, TimeoutWithHttpCode) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(35));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Report several LOCAL_ORIGIN_TIMEOUT with optional Http code 500. Host should be ejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  // Get the configured number of failures and simulate than number of connect failures.
  uint32_t n =
      runtime_.snapshot_.getInteger(Consecutive5xxRuntime, detector->config().consecutive5xx());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout,
                                           absl::optional<uint64_t>(500));
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Wait until it is unejected
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Report several LOCAL_ORIGIN_TIMEOUT with HTTP code other that 500. Node should not be ejected.
  EXPECT_CALL(checker_, check(hosts_[0])).Times(0);
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true))
      .Times(0);
  // Get the configured number of failures and simulate than number of connect failures.
  n = runtime_.snapshot_.getInteger(Consecutive5xxRuntime, detector->config().consecutive5xx());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout,
                                           absl::optional<uint64_t>(200));
  }
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Report LOCAL_ORIGIN_TIMEOUT without explicit HTTP code mapping. It should be implicitly mapped
  // to 5xx code and the node should be ejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  // Get the configured number of failures and simulate than number of connect failures.
  n = runtime_.snapshot_.getInteger(ConsecutiveGatewayFailureRuntime,
                                    detector->config().consecutiveGatewayFailure());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

/*
 * Test validates scenario when large number of timeouts are
 * reported to the outlier detector. The first N timeouts will
 * cause host ejection. Subsequent timeouts, reported while host
 * is ejected, should not affect unejecting the host.
 * Any additional timeouts reported after host has been unejected
 * should cause the host to be ejected again.
 */
TEST_F(OutlierDetectorImplTest, LargeNumberOfTimeouts) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, outlier_detection_split_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveLocalOriginFailureRuntime, 100))
      .WillByDefault(Return(true));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Report several LOCAL_ORIGIN_TIMEOUT. Host should be ejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, true));
  // Get the configured number of failures and simulate than number of timeouts.
  uint32_t n =
      runtime_.snapshot_.getInteger(Consecutive5xxRuntime, detector->config().consecutive5xx());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Simulate more timeouts after the host have been ejected.
  n = 2 * runtime_.snapshot_.getInteger(Consecutive5xxRuntime, detector->config().consecutive5xx());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout);
  }

  // Wait until the host is unejected.
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Simulate more timeouts. The host should be ejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, true));
  n = runtime_.snapshot_.getInteger(Consecutive5xxRuntime, detector->config().consecutive5xx());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginTimeout);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

/**
 * Set of tests to verify ejecting and unejecting nodes when local/connect failures are reported.
 */
TEST_F(OutlierDetectorImplTest, BasicFlowLocalOriginFailure) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"}, true);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, outlier_detection_split_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));

  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveLocalOriginFailureRuntime, 100))
      .WillByDefault(Return(true));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // When connect failure is detected the following methods should be called.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, true));
  time_system_.setMonotonicTime(std::chrono::milliseconds(0));

  // Get the configured number of failures and simulate than number of connect failures.
  uint32_t n = runtime_.snapshot_.getInteger(ConsecutiveLocalOriginFailureRuntime,
                                             detector->config().consecutiveLocalOriginFailure());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Simulate additional number of failures to ensure they don't affect future ejections.
  n = runtime_.snapshot_.getInteger(ConsecutiveLocalOriginFailureRuntime,
                                    detector->config().consecutiveLocalOriginFailure());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  }

  // Wait short time - not enough to be unejected
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(30001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Simulate connection failures to trigger another ejection
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, true));

  n = runtime_.snapshot_.getInteger(ConsecutiveLocalOriginFailureRuntime,
                                    detector->config().consecutiveLocalOriginFailure());
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  }
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  time_system_.setMonotonicTime(std::chrono::milliseconds(90002));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Simulate few connect failures, not enough for ejection and then simulate connect success
  // and again few failures not enough for ejection.
  n = runtime_.snapshot_.getInteger(ConsecutiveLocalOriginFailureRuntime,
                                    detector->config().consecutiveLocalOriginFailure());
  n--; // make sure that this is not enough for ejection.
  while (n--) {
    hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  }
  // now success and few failures
  hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectSuccess);
  hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectFailed);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Check stats
  EXPECT_EQ(
      2UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_enforced_total").value());
  EXPECT_EQ(2UL,
            cluster_.info_->stats_store_
                .counter("outlier_detection.ejections_detected_consecutive_local_origin_failure")
                .value());
  EXPECT_EQ(2UL,
            cluster_.info_->stats_store_
                .counter("outlier_detection.ejections_enforced_consecutive_local_origin_failure")
                .value());
}

/**
 * Test the interaction between the consecutive gateway failure and 5xx detectors.
 * This will first trigger a consecutive gateway failure with 503s, and then trigger 5xx with a mix
 * of 503s and 500s. We expect the consecutive gateway failure to fire after 5 consecutive 503s, and
 * after an uneject the 5xx detector should require a further 5 consecutive 5xxs. The gateway
 * failure detector should not fire a second time since fewer than another 5x 503s are triggered.
 * This will also ensure that the stats counters end up with the expected values.
 */
TEST_F(OutlierDetectorImplTest, BasicFlowGatewayFailureAnd5xx) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));

  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 0))
      .WillByDefault(Return(true));

  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 1, 503);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(30001));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime());

  // Eject host again but with a mix of 500s and 503s to trigger 5xx ejection first
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 2, 503);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  loadRq(hosts_[0], 2, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(40000));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  // Deprecated counter, check we're preserving old behaviour
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      2UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_enforced_total").value());
  EXPECT_EQ(
      1UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_5xx")
                     .value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_5xx")
                     .value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_gateway_failure")
                     .value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_gateway_failure")
                     .value());
}

// Test mapping of Non-Http codes to Http. This happens when split between external and local
// origin errors is turned off.
TEST_F(OutlierDetectorImplTest, BasicFlowNonHttpCodesExternalOrigin) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(true));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 0))
      .WillByDefault(Return(false));

  // Make sure that EXT_ORIGIN_REQUEST_SUCCESS cancels LOCAL_ORIGIN_CONNECT_FAILED
  for (auto i = 0; i < 100; i++) {
    loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
    loadRq(hosts_[0], 1, Result::ExtOriginRequestSuccess);
  }
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause a consecutive 5xx error. This situation happens in router filter.
  // Make sure that one CONNECT_SUCCESS with optional code zero, does not
  // interrupt sequence of LOCAL_ORIGIN_CONNECT_FAILED.
  loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
  hosts_[0]->outlierDetector().putResult(Result::LocalOriginConnectSuccess);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 3, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  EXPECT_CALL(checker_, check(hosts_[0]));
  loadRq(hosts_[0], 1, Result::LocalOriginConnectFailed);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, BasicFlowSuccessRateExternalOrigin) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:82",
      "tcp://127.0.0.1:83",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection to test SR detection in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 100))
      .WillByDefault(Return(false));
  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(40);
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false))
      .Times(40);

  // Cause a SR error on one host. First have 4 of the hosts have perfect SR.
  loadRq(hosts_, 200, 200);
  loadRq(hosts_[4], 200, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::SUCCESS_RATE, true));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  ON_CALL(runtime_.snapshot_, getInteger(SuccessRateStdevFactorRuntime, 1900))
      .WillByDefault(Return(1900));
  interval_timer_->invokeCallback();
  EXPECT_EQ(50, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(90, detector->successRateAverage(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(52, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  // Make sure that local origin success rate monitor is not affected
  EXPECT_EQ(-1, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(19999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(5);
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false))
      .Times(5);

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(hosts_, 25, 200);
  loadRq(hosts_[4], 25, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(60001));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  // The success rate should be *calculated* since the minimum request volume was met for failure
  // percentage ejection, but the host should not be ejected.
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(50UL, hosts_[4]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateAverage(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
}

// Test verifies that EXT_ORIGIN_REQUEST_FAILED and EXT_ORIGIN_REQUEST_SUCCESS cancel
// each other in split mode.
TEST_F(OutlierDetectorImplTest, ExternalOriginEventsWithSplit) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"}, true);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, outlier_detection_split_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));

  for (auto i = 0; i < 100; i++) {
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestFailed);
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestSuccess);
  }
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Now make sure that EXT_ORIGIN_REQUEST_FAILED ejects the host
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  for (auto i = 0; i < 100; i++) {
    hosts_[0]->outlierDetector().putResult(Result::ExtOriginRequestFailed);
  }
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

TEST_F(OutlierDetectorImplTest, BasicFlowSuccessRateLocalOrigin) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:82",
      "tcp://127.0.0.1:83",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, outlier_detection_split_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off detecting consecutive local origin failures.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveLocalOriginFailureRuntime, 100))
      .WillByDefault(Return(false));
  // Expect non-enforcing logging to happen every time the consecutive_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, false))
      .Times(40);
  // Cause a SR error on one host. First have 4 of the hosts have perfect SR.
  loadRq(hosts_, 200, Result::LocalOriginConnectSuccess);
  loadRq(hosts_[4], 200, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::SUCCESS_RATE_LOCAL_ORIGIN, true));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  ON_CALL(runtime_.snapshot_, getInteger(SuccessRateStdevFactorRuntime, 1900))
      .WillByDefault(Return(1900));
  interval_timer_->invokeCallback();
  EXPECT_EQ(50, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(90,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(52, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  // Make sure that external origin success rate monitor is not affected
  EXPECT_EQ(-1, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateAverage(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(19999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Expect non-enforcing logging to happen every time the consecutive_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, false))
      .Times(5);

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(hosts_, 25, Result::LocalOriginConnectSuccess);
  loadRq(hosts_[4], 25, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(60001));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  // The success rate should be *calculated* since the minimum request volume was met for failure
  // percentage ejection, but the host should not be ejected.
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(50UL, hosts_[4]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
}

// Validate that empty hosts doesn't crash success rate handling when success_rate_minimum_hosts is
// zero. This is a regression test for earlier divide-by-zero behavior.
TEST_F(OutlierDetectorImplTest, EmptySuccessRate) {
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  loadRq(hosts_, 200, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  ON_CALL(runtime_.snapshot_, getInteger(SuccessRateMinimumHostsRuntime, 5))
      .WillByDefault(Return(0));
  interval_timer_->invokeCallback();
}

TEST_F(OutlierDetectorImplTest, BasicFlowFailurePercentageExternalOrigin) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:82",
      "tcp://127.0.0.1:83",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection and SR detection to test failure percentage detection in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingSuccessRateRuntime, 100))
      .WillByDefault(Return(false));
  // Now turn on failure percentage detection.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingFailurePercentageRuntime, 0))
      .WillByDefault(Return(true));
  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[3]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(50);
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[3]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false))
      .Times(50);
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(60);
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false))
      .Times(60);

  // Cause a failure percentage error on one host. First 3 hosts have perfect failure percentage;
  // fourth host has failure percentage slightly below threshold; fifth has failure percentage
  // slightly above threshold.
  loadRq(hosts_, 50, 200);
  loadRq(hosts_[3], 250, 503);
  loadRq(hosts_[4], 300, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::FAILURE_PERCENTAGE, true));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  ON_CALL(runtime_.snapshot_, getInteger(SuccessRateStdevFactorRuntime, 1900))
      .WillByDefault(Return(1900));
  interval_timer_->invokeCallback();
  EXPECT_FLOAT_EQ(100.0 * (50.0 / 300.0),
                  hosts_[3]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_FLOAT_EQ(100.0 * (50.0 / 350.0),
                  hosts_[4]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  // Make sure that local origin success rate monitor is not affected
  EXPECT_EQ(-1, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_FALSE(hosts_[3]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(19999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(5);
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false))
      .Times(5);

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(hosts_, 25, 200);
  loadRq(hosts_[4], 25, 503);

  time_system_.setMonotonicTime(std::chrono::milliseconds(60001));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  // The success rate should be *calculated* since the minimum request volume was met for failure
  // percentage ejection, but the host should not be ejected.
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(50UL, hosts_[4]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateAverage(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
}

TEST_F(OutlierDetectorImplTest, BasicFlowFailurePercentageLocalOrigin) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:82",
      "tcp://127.0.0.1:83",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, outlier_detection_split_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection and SR detection to test failure percentage detection in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveLocalOriginFailureRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingLocalOriginSuccessRateRuntime, 100))
      .WillByDefault(Return(false));
  // Now turn on failure percentage detection.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingFailurePercentageLocalOriginRuntime, 0))
      .WillByDefault(Return(true));
  // Expect non-enforcing logging to happen every time the consecutive_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, false))
      .Times(40);
  // Cause a failure percentage error on one host. First 4 of the hosts have perfect failure
  // percentage.
  loadRq(hosts_, 200, Result::LocalOriginConnectSuccess);
  loadRq(hosts_[4], 200, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::FAILURE_PERCENTAGE_LOCAL_ORIGIN, true));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::SUCCESS_RATE_LOCAL_ORIGIN, false));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  ON_CALL(runtime_.snapshot_, getInteger(FailurePercentageThresholdRuntime, 85))
      .WillByDefault(Return(40));
  interval_timer_->invokeCallback();
  EXPECT_EQ(50, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(90,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(52, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  // Make sure that external origin success rate monitor is not affected
  EXPECT_EQ(-1, hosts_[4]->outlierDetector().successRate(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateAverage(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that doesn't bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(19999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Interval that does bring the host back in.
  time_system_.setMonotonicTime(std::chrono::milliseconds(50001));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Expect non-enforcing logging to happen every time the consecutive_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_LOCAL_ORIGIN_FAILURE, false))
      .Times(5);

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(hosts_, 25, Result::LocalOriginConnectSuccess);
  loadRq(hosts_[4], 25, Result::LocalOriginConnectFailed);

  time_system_.setMonotonicTime(std::chrono::milliseconds(60001));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  // The success rate should be *calculated* since the minimum request volume was met for failure
  // percentage ejection, but the host should not be ejected.
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(50UL, hosts_[4]->outlierDetector().successRate(
                      DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1,
            detector->successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
  EXPECT_EQ(-1, detector->successRateEjectionThreshold(
                    DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
}

TEST_F(OutlierDetectorImplTest, RemoveWhileEjected) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  HostVector old_hosts = std::move(hosts_);
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, old_hosts);

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  time_system_.setMonotonicTime(std::chrono::milliseconds(9999));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
}

TEST_F(OutlierDetectorImplTest, Overflow) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80", "tcp://127.0.0.1:81"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(60));

  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  hosts_[0]->outlierDetector().putHttpResponseCode(500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  loadRq(hosts_[1], 5, 500);
  EXPECT_FALSE(hosts_[1]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(1UL,
            cluster_.info_->stats_store_.counter("outlier_detection.ejections_overflow").value());
}

TEST_F(OutlierDetectorImplTest, NotEnforcing) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(35));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80", "tcp://127.0.0.1:81", "tcp://127.0.0.1:82"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 503);

  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(false));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]), _,
                       envoy::data::cluster::v3::CONSECUTIVE_GATEWAY_FAILURE, false));
  loadRq(hosts_[0], 1, 503);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(
      0UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_enforced_total").value());
  EXPECT_EQ(
      1UL,
      cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx").value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_5xx")
                     .value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_5xx")
                     .value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_detected_consecutive_gateway_failure")
                     .value());
  EXPECT_EQ(0UL, cluster_.info_->stats_store_
                     .counter("outlier_detection.ejections_enforced_consecutive_gateway_failure")
                     .value());
}

TEST_F(OutlierDetectorImplTest, EjectionActiveValueIsAccountedWithoutMetricStorage) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80", "tcp://127.0.0.1:81"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(50));

  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));

  // Manually increase the gauge. From metric's perspective it's overflowed.
  outlier_detection_ejections_active_.inc();

  // Since the overflow is not determined by the metric. Host[0] can be ejected.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  hosts_[0]->outlierDetector().putHttpResponseCode(500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Expect active helper_ has the value 1. However, helper is private and it cannot be tested.
  EXPECT_EQ(2UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(0UL,
            cluster_.info_->stats_store_.counter("outlier_detection.ejections_overflow").value());

  // Now it starts to overflow.
  loadRq(hosts_[1], 5, 500);
  EXPECT_FALSE(hosts_[1]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(2UL, outlier_detection_ejections_active_.value());
  EXPECT_EQ(1UL,
            cluster_.info_->stats_store_.counter("outlier_detection.ejections_overflow").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadRemoveRace) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  loadRq(hosts_[0], 1, 500);

  // Remove before the cross thread event comes in.
  HostVector old_hosts = std::move(hosts_);
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadDestroyRace) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  loadRq(hosts_[0], 1, 500);

  // Destroy before the cross thread event comes in.
  std::weak_ptr<DetectorImpl> weak_detector = detector;
  detector.reset();
  EXPECT_EQ(nullptr, weak_detector.lock());
  post_cb();

  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadFailRace) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  loadRq(hosts_[0], 1, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));

  // Fire the post callback twice. This should only result in a single ejection.
  post_cb();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  post_cb();

  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, MaxEjectionPercentage) {
  // A 50% ejection limit should not eject more than 1 out of 3 pods.
  const std::string yaml = R"EOF(
max_ejection_percent: 50
max_ejection_time_jitter: 13s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection_;
  TestUtility::loadFromYaml(yaml, outlier_detection_);
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));

  // add 3 hosts.
  addHosts({"tcp://127.0.0.2:80"});
  addHosts({"tcp://127.0.0.3:80"});
  addHosts({"tcp://127.0.0.4:80"});

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection_, dispatcher_, runtime_, time_system_, event_logger_, random_));

  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  // Expect only one ejection.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));

  loadRq(hosts_[0], 5, 500);
  loadRq(hosts_[1], 5, 500);
  loadRq(hosts_[2], 5, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_FALSE(hosts_[1]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_FALSE(hosts_[2]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

TEST_F(OutlierDetectorImplTest, MaxEjectionPercentageSingleHost) {
  // Single host should not be ejected with a max ejection percent <100% .
  const std::string yaml = R"EOF(
max_ejection_percent: 90
max_ejection_time_jitter: 13s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection_;
  TestUtility::loadFromYaml(yaml, outlier_detection_);
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));

  addHosts({"tcp://127.0.0.2:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection_, dispatcher_, runtime_, time_system_, event_logger_, random_));

  loadRq(hosts_[0], 5, 500);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
}

TEST_F(OutlierDetectorImplTest, Consecutive_5xxAlreadyEjected) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });
  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 4, 500);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause another consecutive 5xx error.
  loadRq(hosts_[0], 1, 200);
  loadRq(hosts_[0], 5, 500);
}

// Test verifies that ejection time increases each time the node is ejected,
// and decreases when node stays healthy.
// The test outline is as follows:
// - eject the node for the first time. It should be brought back in 10 secs
// - eject the node the second time. It should be brought back in 20 secs
// - eject the node the third time. It should be brought back in 30 secs
// - for the next two intervals the node is healthy, which should
//   bring ejection time down.
// - eject the node again. It should be brought back in 20 secs.
// - simulate long period of time when the node is healthy.
// - eject the node again. It should be brought back after 10 secs.
TEST_F(OutlierDetectorImplTest, EjectTimeBackoff) {
  // Setup base ejection time to 10 secs.
  ON_CALL(runtime_.snapshot_, getInteger(BaseEjectionTimeMsRuntime, _))
      .WillByDefault(Return(10000UL));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Eject the node by consecutive 5xx errors.
  time_system_.setMonotonicTime(std::chrono::seconds(0));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that node has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Ejection base time is 10 secs. The node has been ejected just once.
  // It should be brought back after 10 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  // Make sure that node has been brought back.
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Cause ejection again.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that node has been ejected.
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // This is the second ejection in the row.
  // Node should stay ejected for twice the base_ejection_time: 20 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(20));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  // Make sure that node stays ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
  time_system_.setMonotonicTime(std::chrono::seconds(30));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  // Make sure that node has been brought back.
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Third ejection in the row. It starts at 30 secs. The node should be ejected for 3*10 secs.
  // It should not be brought back until 60 secs from the start of the test.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that node has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Node should stay ejected after 10 secs of ejection time.
  time_system_.setMonotonicTime(std::chrono::seconds(40));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Node should stay ejected after 20 secs of ejection time.
  time_system_.setMonotonicTime(std::chrono::seconds(50));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Node should be brought back after being ejected for 30 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(60));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  // Make sure that node has been brought back.
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // During the next 2 timer intervals, the node is healthy. This should decrease
  // the eject time backoff.
  time_system_.setMonotonicTime(std::chrono::seconds(70));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  time_system_.setMonotonicTime(std::chrono::seconds(80));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();

  // Trigger the next ejection. The node should be ejected for 20 secs.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that node has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Node should stay ejected after 10 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(90));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Node should be brought back after being ejected for 20 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(100));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  // Make sure that node has been brought back.
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Now simulate long period of no errors.
  // The node will not be ejected and the eject backoff time should
  // drop to the initial value of 1 * base_ejection_time.
  for (auto i = 1; i <= 50; i++) {
    time_system_.setMonotonicTime(std::chrono::seconds(100 + i * 10));
    EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
    interval_timer_->invokeCallback();
  }

  // Trigger ejection.
  time_system_.setMonotonicTime(std::chrono::seconds(610));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that node has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // The node should be brought back after 10 secs.
  time_system_.setMonotonicTime(std::chrono::seconds(620));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  // Make sure that node has brought back.
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
}

TEST_F(OutlierDetectorImplTest, EjectTimeBackoffTimeBasedDetection) {
  // Setup base ejection time to 10 secs.
  const uint64_t base_ejection_time = 10000;
  ON_CALL(runtime_.snapshot_, getInteger(BaseEjectionTimeMsRuntime, _))
      .WillByDefault(Return(base_ejection_time));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection to test failure percentage in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutive5xxRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingConsecutiveGatewayFailureRuntime, 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_, getInteger(FailurePercentageMinimumHostsRuntime, 5))
      .WillByDefault(Return(1));
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingSuccessRateRuntime, 100))
      .WillByDefault(Return(false));
  // Disable jitter.
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionTimeJitterMsRuntime, _))
      .WillByDefault(Return(0UL));
  // Turn on failure percentage detection.
  ON_CALL(runtime_.snapshot_, featureEnabled(EnforcingFailurePercentageRuntime, 0))
      .WillByDefault(Return(true));

  time_system_.setMonotonicTime(std::chrono::seconds(0));

  // Simulate 100% failure.
  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(20);
  loadRq(hosts_[0], 100, 500);

  uint32_t tick = 1;
  // Invoke periodic timer. The node should be ejected.
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::FAILURE_PERCENTAGE, true));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Advance time to the next periodic timer tick. Node should be unejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Keep replying with 5xx error codes.
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(20);
  loadRq(hosts_[0], 100, 500);

  // Advance time to the next periodic timer tick. Node should be ejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::FAILURE_PERCENTAGE, true));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // The node was ejected the second time in a row. The length of time the node
  // should be ejected should be increased.
  // Advance time to the next periodic timer tick. Node should stay ejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Advance time to next periodic timer tick. This time the node should be unejected
  // (after 2 periods of base_ejection_time).
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Return success during the next two timer periods. This should decrement ejection time.
  loadRq(hosts_[0], 100, 200);

  // Advance time to the next periodic time tick. The node should stay unejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  loadRq(hosts_[0], 100, 200);
  // Advance time to the next periodic time tick. The node should stay unejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());

  // Return errors during the next period.
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, false))
      .Times(20);
  loadRq(hosts_[0], 100, 500);

  // Advance time to next periodic timer tick. Node should be ejected.
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::FAILURE_PERCENTAGE, true));
  interval_timer_->invokeCallback();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

  // Advance time to the next periodic timer tick. This time the node should be unejected
  // only after ONE period of base_ejection_time (ejection time was reduced when the node
  // did not fail for two timer periods).
  tick++;
  time_system_.setMonotonicTime(std::chrono::milliseconds(base_ejection_time * tick));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(base_ejection_time), _));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  interval_timer_->invokeCallback();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
}

// Test that ejection time does not increase beyond maximum.
// Test outline:
// - max_ejection_time is 30 times longer than base_ejection_time.
// - simulate 30 ejections. Each time the node is ejected, the ejection time is
//   longer. The last ejection time is equal to max_ejection_time.
// - eject node again. Ejection time should not increase beyond max_ejection_time.
TEST_F(OutlierDetectorImplTest, MaxEjectTime) {
  // Setup base ejection time to 10 secs.
  ON_CALL(runtime_.snapshot_, getInteger(BaseEjectionTimeMsRuntime, _))
      .WillByDefault(Return(10000UL));
  // Setup max ejection time to 30 secs.
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionTimeMsRuntime, _))
      .WillByDefault(Return(300000UL));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Verify that maximum_ejection_time caps ejection time.
  // Base ejection time is 10s. Max ejection time is 300s.
  // It will take 30 ejections to reach the maximum, Beyond that, ejection time should stay
  // the same
  uint32_t eject_tick = 0;
  time_system_.setMonotonicTime(std::chrono::seconds(0));
  // Trigger 30 ejection.
  // For each ejection, time to uneject increases.
  for (auto i = 1; i <= 30; i++) {
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                         _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
    loadRq(hosts_[0], 5, 500);
    // Make sure that node has been ejected.
    EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

    // Simulate several check intervals. For each check the node should stay ejected.
    for (auto j = 1; j < i; j++) {
      time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
      EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
      interval_timer_->invokeCallback();
      EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
      EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
    }

    // Wait for unejection.
    time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
    EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_,
                logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
    interval_timer_->invokeCallback();
    // Make sure that node has been brought back.
    EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  }

  // Keep ejecting the node. Ejection time should not increase.
  for (auto i = 1; i < 10; i++) {
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                         _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
    loadRq(hosts_[0], 5, 500);
    // Make sure that node has been ejected.
    EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

    // Move the time 290s ahead. The ejection should not happen.
    for (auto j = 1; j <= 29; j++) {
      time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
      EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
      interval_timer_->invokeCallback();
      EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
      EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
    }

    // Node should be brought back after 300 secs.
    time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
    EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_,
                logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
    interval_timer_->invokeCallback();
    // Make sure that node has been ejected.
    EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  }
}

// Test that maximum ejection time logic behaves properly when
// max_ejection_time is not multitude of base_ejection_time.
// The same test as MaxEjectTime, but with config where
// max_ejection_time is not multiple of base_ejection_time.
// Because ejection time increases in base_ejection_time intervals,
// the maximum ejection time will be equal to
// max_ejection_time + base_ejection_time.
TEST_F(OutlierDetectorImplTest, MaxEjectTimeNotAlligned) {

  // Setup interval time to 10 secs.
  ON_CALL(runtime_.snapshot_, getInteger(IntervalMsRuntime, _)).WillByDefault(Return(10000UL));
  ON_CALL(runtime_.snapshot_, getInteger(BaseEjectionTimeMsRuntime, _))
      .WillByDefault(Return(10000UL));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionTimeMsRuntime, _))
      .WillByDefault(Return(305000UL));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Verify that maximum_ejection_time caps ejection time.
  // Base ejection time is 10s. Max ejection time is 305s.
  uint32_t eject_tick = 0;
  time_system_.setMonotonicTime(std::chrono::seconds(0));
  // Trigger 31 ejections in a row.
  // For each ejection, time to uneject increases.
  for (auto i = 1; i <= 31; i++) {
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                         _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
    loadRq(hosts_[0], 5, 500);
    // Make sure that node has been ejected.
    EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

    // Simulate several intervals. For check the node should stay ejected.
    for (auto j = 1; j < i; j++) {
      time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
      EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
      interval_timer_->invokeCallback();
      EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
      EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
    }

    // Wait for unejection.
    time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
    EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_,
                logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
    interval_timer_->invokeCallback();
    // Make sure that node has been brought back.
    EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  }

  // Keep ejecting the node. Ejection time should not increase.
  for (auto i = 1; i < 10; i++) {
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                         _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
    loadRq(hosts_[0], 5, 500);
    // Make sure that node has been ejected.
    EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());

    // Move the time 300s ahead. The node should stay ejected.
    for (auto j = 1; j <= 30; j++) {
      time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
      EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
      interval_timer_->invokeCallback();
      EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
      EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
    }

    // Move time one base_ejection_time beyond max_ejection_time.
    // Wait for unejection.
    time_system_.setMonotonicTime(std::chrono::seconds(++eject_tick * 10));
    EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
    EXPECT_CALL(checker_, check(hosts_[0]));
    EXPECT_CALL(*event_logger_,
                logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
    interval_timer_->invokeCallback();
    // Make sure that node has been brought back.
    EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
    EXPECT_EQ(0UL, outlier_detection_ejections_active_.value());
  }
}

// Test verifies that the detector is properly initialized with
// max_ejection_time_jitter when it is specified in the
// the static config
TEST_F(OutlierDetectorImplTest, DetectorStaticConfigMaxEjectionTimeJitter) {
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 10s
max_ejection_time_jitter: 13s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(10000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(13000UL, detector->config().maxEjectionTimeJitterMs());
}

// Test verifies that detector is properly initialized with
// default max_ejection_time_jitter when it is
// not specified in config.
TEST_F(OutlierDetectorImplTest, DetectorStaticConfigDefaultMaxEjectionTimeJitter) {
  const std::string yaml = R"EOF(
interval: 0.1s
base_ejection_time: 10s
  )EOF";
  envoy::config::cluster::v3::OutlierDetection outlier_detection;
  TestUtility::loadFromYaml(yaml, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_system_, event_logger_, random_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(10000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(0UL, detector->config().maxEjectionTimeJitterMs());
}

// Test verifies that jitter is between 0s and 10s
// when max_ejection_time_jitter is set to 10s.
TEST_F(OutlierDetectorImplTest, EjectionTimeJitterIsInRange) {
  // Setup max_ejection_time_jitter time to 10 secs.
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionTimeJitterMsRuntime, _))
      .WillByDefault(Return(10000UL));
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  // Add host.
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });
  // Set the return value of random().
  EXPECT_CALL(random_, random()).WillOnce(Return(123456789UL));
  // Trigger host eject.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that the host has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
  // Get the jitter from the host monitor
  // and make sure that it is 4445.
  auto host_monitor = detector->getHostMonitors().find(hosts_[0])->second;
  uint64_t jitter = host_monitor->getJitter().count();
  EXPECT_EQ(4445UL, jitter);
}

// Test verifies that jitter is 0 when the
// max_ejection_time_jitter configuration is absent.
TEST_F(OutlierDetectorImplTest, EjectionTimeJitterIsZeroWhenNotConfigured) {
  ON_CALL(runtime_.snapshot_, getInteger(MaxEjectionPercentRuntime, _)).WillByDefault(Return(100));
  // Add host with empty configurations. Max_eject_time_jitter will
  // default to 0.
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000), _));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(cluster_, empty_outlier_detection_,
                                                              dispatcher_, runtime_, time_system_,
                                                              event_logger_, random_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });
  // Set the return value of random().
  EXPECT_CALL(random_, random()).WillOnce(Return(1234567890UL));
  // Trigger host eject.
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, envoy::data::cluster::v3::CONSECUTIVE_5XX, true));
  loadRq(hosts_[0], 5, 500);
  // Make sure that the host has been ejected.
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, outlier_detection_ejections_active_.value());
  // Get the jitter from the host monitor
  // and make sure that it is zero.
  auto host_monitor = detector->getHostMonitors().find(hosts_[0])->second;
  uint64_t jitter = host_monitor->getJitter().count();
  EXPECT_EQ(0UL, jitter);
}

TEST(DetectorHostMonitorNullImplTest, All) {
  DetectorHostMonitorNullImpl null_sink;

  EXPECT_EQ(0UL, null_sink.numEjections());
  EXPECT_FALSE(null_sink.lastEjectionTime());
  EXPECT_FALSE(null_sink.lastUnejectionTime());
}

TEST(OutlierDetectionEventLoggerImplTest, All) {
  AccessLog::MockAccessLogManager log_manager;
  std::shared_ptr<AccessLog::MockAccessLogFile> file(new AccessLog::MockAccessLogFile());
  NiceMock<MockClusterInfo> cluster;
  std::shared_ptr<MockHostDescription> host(new NiceMock<MockHostDescription>());
  ON_CALL(*host, cluster()).WillByDefault(ReturnRef(cluster));
  Event::SimulatedTimeSystem time_system;
  // This is rendered as "2018-12-18T09:00:00Z"
  time_system.setSystemTime(std::chrono::milliseconds(1545123600000));
  absl::optional<MonotonicTime> monotonic_time;
  NiceMock<MockDetector> detector;

  EXPECT_CALL(log_manager, createAccessLog(Filesystem::FilePathAndType{
                               Filesystem::DestinationType::File, "foo"}))
      .WillOnce(Return(file));
  EventLoggerImpl event_logger(log_manager, "foo", time_system);

  StringViewSaver log1;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(monotonic_time));

  EXPECT_CALL(*file,
              write(absl::string_view(
                  "{\"type\":\"CONSECUTIVE_5XX\",\"timestamp\":\"2018-12-18T09:00:00Z\","
                  "\"cluster_name\":\"fake_cluster\","
                  "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"EJECT\","
                  "\"num_ejections\":0,\"enforced\":true,\"eject_consecutive_event\":{}}\n")))
      .WillOnce(SaveArg<0>(&log1));

  event_logger.logEject(host, detector, envoy::data::cluster::v3::CONSECUTIVE_5XX, true);
  Json::Factory::loadFromString(log1);

  StringViewSaver log2;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(monotonic_time));

  EXPECT_CALL(*file, write(absl::string_view(
                         "{\"type\":\"CONSECUTIVE_5XX\",\"timestamp\":\"2018-12-18T09:00:00Z\","
                         "\"cluster_name\":\"fake_cluster\","
                         "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"UNEJECT\","
                         "\"num_ejections\":0,\"enforced\":false}\n")))
      .WillOnce(SaveArg<0>(&log2));

  event_logger.logUneject(host);
  Json::Factory::loadFromString(log2);

  // now test with time since last action.
  monotonic_time = (time_system.monotonicTime() - std::chrono::seconds(30));

  StringViewSaver log3;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(host->outlier_detector_,
              successRate(DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillOnce(Return(0));
  EXPECT_CALL(detector,
              successRateAverage(DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillOnce(Return(0));
  EXPECT_CALL(detector, successRateEjectionThreshold(
                            DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillOnce(Return(0));
  EXPECT_CALL(*file, write(absl::string_view(
                         "{\"type\":\"SUCCESS_RATE\","
                         "\"timestamp\":\"2018-12-18T09:00:00Z\",\"secs_since_last_action\":\"30\","
                         "\"cluster_name\":\"fake_cluster\","
                         "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"EJECT\","
                         "\"num_ejections\":0,\"enforced\":false,\"eject_success_rate_event\":{"
                         "\"host_success_rate\":0,\"cluster_average_success_rate\":0,"
                         "\"cluster_success_rate_ejection_threshold\":0}}\n")))
      .WillOnce(SaveArg<0>(&log3));
  event_logger.logEject(host, detector, envoy::data::cluster::v3::SUCCESS_RATE, false);
  Json::Factory::loadFromString(log3);

  StringViewSaver log4;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(*file, write(absl::string_view(
                         "{\"type\":\"CONSECUTIVE_5XX\","
                         "\"timestamp\":\"2018-12-18T09:00:00Z\",\"secs_since_last_action\":\"30\","
                         "\"cluster_name\":\"fake_cluster\","
                         "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"UNEJECT\","
                         "\"num_ejections\":0,\"enforced\":false}\n")))
      .WillOnce(SaveArg<0>(&log4));
  event_logger.logUneject(host);
  Json::Factory::loadFromString(log4);

  StringViewSaver log5;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(host->outlier_detector_,
              successRate(DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillOnce(Return(0));
  EXPECT_CALL(*file,
              write(absl::string_view(
                  "{\"type\":\"FAILURE_PERCENTAGE\","
                  "\"timestamp\":\"2018-12-18T09:00:00Z\",\"secs_since_last_action\":\"30\","
                  "\"cluster_name\":\"fake_cluster\","
                  "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"EJECT\","
                  "\"num_ejections\":0,\"enforced\":false,\"eject_failure_percentage_event\":{"
                  "\"host_success_rate\":0}}\n")))
      .WillOnce(SaveArg<0>(&log5));
  event_logger.logEject(host, detector, envoy::data::cluster::v3::FAILURE_PERCENTAGE, false);
  Json::Factory::loadFromString(log5);

  StringViewSaver log6;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(*file, write(absl::string_view(
                         "{\"type\":\"CONSECUTIVE_5XX\","
                         "\"timestamp\":\"2018-12-18T09:00:00Z\",\"secs_since_last_action\":\"30\","
                         "\"cluster_name\":\"fake_cluster\","
                         "\"upstream_url\":\"10.0.0.1:443\",\"action\":\"UNEJECT\","
                         "\"num_ejections\":0,\"enforced\":false}\n")))
      .WillOnce(SaveArg<0>(&log6));
  event_logger.logUneject(host);
  Json::Factory::loadFromString(log6);
}

TEST(OutlierUtility, SRThreshold) {
  std::vector<HostSuccessRatePair> data = {
      HostSuccessRatePair(nullptr, 50),  HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100), HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100),
  };
  double sum = 450;

  DetectorImpl::EjectionPair success_rate_nums =
      DetectorImpl::successRateEjectionThreshold(sum, data, 1.9);
  EXPECT_EQ(90.0, success_rate_nums.success_rate_average_); // average success rate
  EXPECT_EQ(52.0, success_rate_nums.ejection_threshold_);   //  ejection threshold
}

} // namespace
} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
