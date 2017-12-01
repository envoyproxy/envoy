#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optional.h"
#include "envoy/common/time.h"

#include "common/network/utility.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Upstream {
namespace Outlier {

TEST(OutlierDetectorImplFactoryTest, NoDetector) {
  NiceMock<MockCluster> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  EXPECT_EQ(nullptr,
            DetectorImplFactory::createForCluster(cluster, defaultStaticCluster("fake_cluster"),
                                                  dispatcher, runtime, nullptr));
}

TEST(OutlierDetectorImplFactoryTest, Detector) {
  auto fake_cluster = defaultStaticCluster("fake_cluster");
  fake_cluster.mutable_outlier_detection();

  NiceMock<MockCluster> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  EXPECT_NE(nullptr, DetectorImplFactory::createForCluster(cluster, fake_cluster, dispatcher,
                                                           runtime, nullptr));
}

class CallbackChecker {
public:
  MOCK_METHOD1(check, void(HostSharedPtr host));
};

class OutlierDetectorImplTest : public testing::Test {
public:
  OutlierDetectorImplTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_success_rate", 100))
        .WillByDefault(Return(true));
  }

  void addHosts(std::vector<std::string> urls, bool primary = true) {
    std::vector<HostSharedPtr>& hosts = primary ? hosts_ : failover_hosts_;
    for (auto& url : urls) {
      hosts.emplace_back(makeTestHost(cluster_.info_, url));
    }
  }

  void loadRq(std::vector<HostSharedPtr>& hosts, int num_rq, int http_code) {
    for (uint64_t i = 0; i < hosts.size(); i++) {
      loadRq(hosts[i], num_rq, http_code);
    }
  }

  void loadRq(HostSharedPtr host, int num_rq, int http_code) {
    for (int i = 0; i < num_rq; i++) {
      host->outlierDetector().putHttpResponseCode(http_code);
    }
  }

  NiceMock<MockCluster> cluster_;
  std::vector<HostSharedPtr>& hosts_ = cluster_.prioritySet().getMockHostSet(0)->hosts_;
  std::vector<HostSharedPtr>& failover_hosts_ = cluster_.prioritySet().getMockHostSet(1)->hosts_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* interval_timer_ = new Event::MockTimer(&dispatcher_);
  CallbackChecker checker_;
  MockMonotonicTimeSource time_source_;
  std::shared_ptr<MockEventLogger> event_logger_{new MockEventLogger()};
  envoy::api::v2::Cluster::OutlierDetection empty_outlier_detection_;
};

TEST_F(OutlierDetectorImplTest, DetectorStaticConfig) {
  const std::string json = R"EOF(
  {
    "interval_ms" : 100,
    "base_ejection_time_ms" : 10000,
    "consecutive_5xx" : 10,
    "max_ejection_percent" : 50,
    "enforcing_consecutive_5xx" : 10,
    "enforcing_success_rate": 20,
    "success_rate_minimum_hosts": 50,
    "success_rate_request_volume": 200,
    "success_rate_stdev_factor": 3000
  }
  )EOF";

  envoy::api::v2::Cluster::OutlierDetection outlier_detection;
  Json::ObjectSharedPtr custom_config = Json::Factory::loadFromString(json);
  Config::CdsJson::translateOutlierDetection(*custom_config, outlier_detection);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, outlier_detection, dispatcher_, runtime_, time_source_, event_logger_));

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
}

TEST_F(OutlierDetectorImplTest, DestroyWithActive) {
  ON_CALL(runtime_.snapshot_, getInteger("outlier_detection.max_ejection_percent", _))
      .WillByDefault(Return(100));
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"}, true);
  addHosts({"tcp://127.0.0.1:81"}, false);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  loadRq(failover_hosts_[0], 4, 500);
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(failover_hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(failover_hosts_[0]), _,
                       EjectionType::Consecutive5xx, true));
  loadRq(failover_hosts_[0], 1, 500);
  EXPECT_TRUE(failover_hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(2UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  detector.reset();
  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, DestroyHostInUse) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  detector.reset();

  loadRq(hosts_[0], 5, 500);
}

TEST_F(OutlierDetectorImplTest, BasicFlow5xx) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 1, 500);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(30001))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(40000))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
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
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));

  ON_CALL(runtime_.snapshot_,
          featureEnabled("outlier_detection.enforcing_consecutive_gateway_failure", 0))
      .WillByDefault(Return(true));
  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
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
                                       _, EjectionType::Consecutive5xx, false));
  loadRq(hosts_[0], 2, 503);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::ConsecutiveGatewayFailure, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(30001))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Eject host again to cause an ejection after an unejection has taken place
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(40000))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::ConsecutiveGatewayFailure, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
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

/**
 * Test the interaction between the consecutive gateway failure and 5xx detectors.
 * This will first trigger a consecutive gateway failure with 503s, and then trigger 5xx with a mix
 * of 503s and 500s. We expect the consecutive gateway failure to fire after 5 consecutive 503s, and
 * after an uneject the 5xx detector should require a further 5 consecutive 5xxs. The gateway
 * failure detector should not fire a second time since fewer than another 5x 503s are triggered.
 * This will also ensure that the stats counters end up with the expected values.
 */
TEST_F(OutlierDetectorImplTest, BasicFlowGatewayFailureAnd5xx) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));

  ON_CALL(runtime_.snapshot_,
          featureEnabled("outlier_detection.enforcing_consecutive_gateway_failure", 0))
      .WillByDefault(Return(true));

  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  addHosts({"tcp://127.0.0.1:81"});
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 1, 503);
  loadRq(hosts_[0], 1, 200);
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::ConsecutiveGatewayFailure, true));
  loadRq(hosts_[0], 1, 503);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(30001))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_TRUE(hosts_[0]->outlierDetector().lastUnejectionTime().valid());

  // Eject host again but with a mix of 500s and 503s to trigger 5xx ejection first
  hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(hosts_[0], 2, 503);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  loadRq(hosts_[0], 2, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(40000))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, hosts_);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
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

TEST_F(OutlierDetectorImplTest, BasicFlowSuccessRate) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({
      "tcp://127.0.0.1:80",
      "tcp://127.0.0.1:81",
      "tcp://127.0.0.1:82",
      "tcp://127.0.0.1:83",
      "tcp://127.0.0.1:84",
  });

  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection to test SR detection in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
      .WillByDefault(Return(false));
  ON_CALL(runtime_.snapshot_,
          featureEnabled("outlier_detection.enforcing_consecutive_gateway_failure", 100))
      .WillByDefault(Return(false));
  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, EjectionType::Consecutive5xx, false))
      .Times(40);
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, EjectionType::ConsecutiveGatewayFailure, false))
      .Times(40);

  // Cause a SR error on one host. First have 4 of the hosts have perfect SR.
  loadRq(hosts_, 200, 200);
  loadRq(hosts_[4], 200, 503);

  EXPECT_CALL(time_source_, currentTime())
      .Times(2)
      .WillRepeatedly(Return(MonotonicTime(std::chrono::milliseconds(10000))));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, EjectionType::SuccessRate, true));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  ON_CALL(runtime_.snapshot_, getInteger("outlier_detection.success_rate_stdev_factor", 1900))
      .WillByDefault(Return(1900));
  interval_timer_->callback_();
  EXPECT_EQ(50, hosts_[4]->outlierDetector().successRate());
  EXPECT_EQ(90, detector->successRateAverage());
  EXPECT_EQ(52, detector->successRateEjectionThreshold());
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(19999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_TRUE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(50001))));
  EXPECT_CALL(checker_, check(hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Expect non-enforcing logging to happen every time the consecutive_5xx_ counter
  // gets saturated (every 5 times).
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, EjectionType::Consecutive5xx, false))
      .Times(5);
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[4]),
                                       _, EjectionType::ConsecutiveGatewayFailure, false))
      .Times(5);

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(hosts_, 25, 200);
  loadRq(hosts_[4], 25, 503);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(60001))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
  EXPECT_EQ(-1, hosts_[4]->outlierDetector().successRate());
  EXPECT_EQ(-1, detector->successRateAverage());
  EXPECT_EQ(-1, detector->successRateEjectionThreshold());
}

TEST_F(OutlierDetectorImplTest, RemoveWhileEjected) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  std::vector<HostSharedPtr> old_hosts = std::move(hosts_);
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, old_hosts);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
}

TEST_F(OutlierDetectorImplTest, Overflow) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80", "tcp://127.0.0.1:81"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  ON_CALL(runtime_.snapshot_, getInteger("outlier_detection.max_ejection_percent", _))
      .WillByDefault(Return(1));

  loadRq(hosts_[0], 4, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  hosts_[0]->outlierDetector().putHttpResponseCode(500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  loadRq(hosts_[1], 5, 500);
  EXPECT_FALSE(hosts_[1]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL,
            cluster_.info_->stats_store_.counter("outlier_detection.ejections_overflow").value());
}

TEST_F(OutlierDetectorImplTest, NotEnforcing) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 503);

  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
      .WillByDefault(Return(false));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, false));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::ConsecutiveGatewayFailure, false));
  loadRq(hosts_[0], 1, 503);
  EXPECT_FALSE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
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

TEST_F(OutlierDetectorImplTest, CrossThreadRemoveRace) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(hosts_[0], 1, 500);

  // Remove before the cross thread event comes in.
  std::vector<HostSharedPtr> old_hosts = std::move(hosts_);
  cluster_.prioritySet().getMockHostSet(0)->runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadDestroyRace) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(hosts_[0], 1, 500);

  // Destroy before the cross thread event comes in.
  std::weak_ptr<DetectorImpl> weak_detector = detector;
  detector.reset();
  EXPECT_EQ(nullptr, weak_detector.lock());
  post_cb();

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadFailRace) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(hosts_[0], 4, 500);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(hosts_[0], 1, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));

  // Fire the post callback twice. This should only result in a single ejection.
  post_cb();
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  post_cb();

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, Consecutive5xxAlreadyEjected) {
  EXPECT_CALL(cluster_.prioritySet(), addMemberUpdateCb(_));
  addHosts({"tcp://127.0.0.1:80"});
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, empty_outlier_detection_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Cause a consecutive 5xx error.
  loadRq(hosts_[0], 4, 500);

  EXPECT_CALL(time_source_, currentTime())
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(hosts_[0]));
  EXPECT_CALL(*event_logger_, logEject(std::static_pointer_cast<const HostDescription>(hosts_[0]),
                                       _, EjectionType::Consecutive5xx, true));
  loadRq(hosts_[0], 1, 500);
  EXPECT_TRUE(hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause another consecutive 5xx error.
  loadRq(hosts_[0], 1, 200);
  loadRq(hosts_[0], 5, 500);
}

TEST(DetectorHostMonitorNullImplTest, All) {
  DetectorHostMonitorNullImpl null_sink;

  EXPECT_EQ(0UL, null_sink.numEjections());
  EXPECT_FALSE(null_sink.lastEjectionTime().valid());
  EXPECT_FALSE(null_sink.lastUnejectionTime().valid());
}

TEST(OutlierDetectionEventLoggerImplTest, All) {
  AccessLog::MockAccessLogManager log_manager;
  std::shared_ptr<Filesystem::MockFile> file(new Filesystem::MockFile());
  NiceMock<MockClusterInfo> cluster;
  std::shared_ptr<MockHostDescription> host(new NiceMock<MockHostDescription>());
  ON_CALL(*host, cluster()).WillByDefault(ReturnRef(cluster));
  NiceMock<MockSystemTimeSource> time_source;
  NiceMock<MockMonotonicTimeSource> monotonic_time_source;
  Optional<SystemTime> time;
  Optional<MonotonicTime> monotonic_time;
  NiceMock<MockDetector> detector;

  EXPECT_CALL(log_manager, createAccessLog("foo")).WillOnce(Return(file));
  EventLoggerImpl event_logger(log_manager, "foo", time_source, monotonic_time_source);

  std::string log1;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"-1\", \"cluster\": "
                           "\"fake_cluster\", \"upstream_url\": \"10.0.0.1:443\", \"action\": "
                           "\"eject\", \"type\": \"5xx\", \"num_ejections\": \"0\", "
                           "\"enforced\": \"true\"}\n"))
      .WillOnce(SaveArg<0>(&log1));
  event_logger.logEject(host, detector, EjectionType::Consecutive5xx, true);
  Json::Factory::loadFromString(log1);

  std::string log2;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"-1\", \"cluster\": \"fake_cluster\", "
                           "\"upstream_url\": \"10.0.0.1:443\", \"action\": \"uneject\", "
                           "\"num_ejections\": 0}\n"))
      .WillOnce(SaveArg<0>(&log2));
  event_logger.logUneject(host);
  Json::Factory::loadFromString(log2);

  // now test with time since last action.
  time.value(time_source.currentTime() - std::chrono::seconds(30));
  monotonic_time.value(monotonic_time_source.currentTime() - std::chrono::seconds(30));

  std::string log3;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(host->outlier_detector_, successRate()).WillOnce(Return(-1));
  EXPECT_CALL(detector, successRateAverage()).WillOnce(Return(-1));
  EXPECT_CALL(detector, successRateEjectionThreshold()).WillOnce(Return(-1));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"30\", \"cluster\": "
                           "\"fake_cluster\", \"upstream_url\": \"10.0.0.1:443\", \"action\": "
                           "\"eject\", \"type\": \"SuccessRate\", \"num_ejections\": \"0\", "
                           "\"enforced\": \"false\", "
                           "\"host_success_rate\": \"-1\", \"cluster_average_success_rate\": "
                           "\"-1\", \"cluster_success_rate_ejection_threshold\": \"-1\""
                           "}\n"))
      .WillOnce(SaveArg<0>(&log3));
  event_logger.logEject(host, detector, EjectionType::SuccessRate, false);
  Json::Factory::loadFromString(log3);

  std::string log4;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(monotonic_time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"30\", \"cluster\": \"fake_cluster\", "
                           "\"upstream_url\": \"10.0.0.1:443\", \"action\": \"uneject\", "
                           "\"num_ejections\": 0}\n"))
      .WillOnce(SaveArg<0>(&log4));
  event_logger.logUneject(host);
  Json::Factory::loadFromString(log4);
}

TEST(OutlierUtility, SRThreshold) {
  std::vector<HostSuccessRatePair> data = {
      HostSuccessRatePair(nullptr, 50),  HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100), HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100),
  };
  double sum = 450;

  Utility::EjectionPair ejection_pair = Utility::successRateEjectionThreshold(sum, data, 1.9);
  EXPECT_EQ(52.0, ejection_pair.ejection_threshold_);
  EXPECT_EQ(90.0, ejection_pair.success_rate_average_);
}

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
