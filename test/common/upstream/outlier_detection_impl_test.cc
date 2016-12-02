#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Upstream {
namespace Outlier {

TEST(OutlierDetectorImplFactoryTest, NoDetector) {
  Json::ObjectPtr loader = Json::Factory::LoadFromString("{}");
  NiceMock<MockCluster> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl stats_store;
  EXPECT_EQ(nullptr, DetectorImplFactory::createForCluster(cluster, *loader, dispatcher, runtime,
                                                           stats_store, nullptr));
}

TEST(OutlierDetectorImplFactoryTest, Detector) {
  std::string json = R"EOF(
  {
    "outlier_detection": {}
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  NiceMock<MockCluster> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  Stats::IsolatedStoreImpl stats_store;
  EXPECT_NE(nullptr, DetectorImplFactory::createForCluster(cluster, *loader, dispatcher, runtime,
                                                           stats_store, nullptr));
}

class CallbackChecker {
public:
  MOCK_METHOD1(check, void(HostPtr host));
};

class OutlierDetectorImplTest : public testing::Test {
public:
  OutlierDetectorImplTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing", 100))
        .WillByDefault(Return(true));
  }

  NiceMock<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* interval_timer_ = new Event::MockTimer(&dispatcher_);
  Stats::IsolatedStoreImpl stats_store_;
  CallbackChecker checker_;
  MockSystemTimeSource time_source_;
  std::shared_ptr<MockEventLogger> event_logger_{new MockEventLogger()};
};

TEST_F(OutlierDetectorImplTest, BasicFlow) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_.push_back(HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:81", false, 1, "")});
  cluster_.runCallbacks({cluster_.hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(200);
  cluster_.hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(30001))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  cluster_.runCallbacks({}, cluster_.hosts_);

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL,
            stats_store_.counter("cluster.fake_cluster.outlier_detection.ejections_total").value());
  EXPECT_EQ(1UL,
            stats_store_.counter("cluster.fake_cluster.outlier_detection.ejections_consecutive_5xx")
                .value());
}

TEST_F(OutlierDetectorImplTest, RemoveWhileEjected) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());

  std::vector<HostPtr> old_hosts = std::move(cluster_.hosts_);
  cluster_.runCallbacks({}, old_hosts);

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
}

TEST_F(OutlierDetectorImplTest, Overflow) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  ON_CALL(runtime_.snapshot_, getInteger("outlier_detection.max_ejection_percent", _))
      .WillByDefault(Return(0));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_FALSE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.outlier_detection.ejections_overflow")
                     .value());
}

TEST_F(OutlierDetectorImplTest, NotEnforcing) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing", 100))
      .WillByDefault(Return(false));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_FALSE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL,
            stats_store_.counter("cluster.fake_cluster.outlier_detection.ejections_total").value());
  EXPECT_EQ(1UL,
            stats_store_.counter("cluster.fake_cluster.outlier_detection.ejections_consecutive_5xx")
                .value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadRemoveRace) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  // Remove before the cross thread event comes in.
  std::vector<HostPtr> old_hosts = std::move(cluster_.hosts_);
  cluster_.runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadFailRace) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  // Fake another event coming in which sets failed, then fire the callback. We should not actually
  // eject anything.
  cluster_.hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  post_cb();

  EXPECT_EQ(0UL,
            stats_store_.gauge("cluster.fake_cluster.outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, Consecutive5xxAlreadyEjected) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostPtr{new HostImpl(cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  DetectorImpl detector(cluster_, dispatcher_, runtime_, stats_store_, time_source_, event_logger_);
  detector.addChangedStateCb([&](HostPtr host) -> void { checker_.check(host); });

  // Cause a consecutive 5xx error.
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause another consecutive 5xx error.
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(200);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
}

TEST(OutlierDetectionEventLoggerImplTest, All) {
  AccessLog::MockAccessLogManager log_manager;
  std::shared_ptr<Filesystem::MockFile> file(new Filesystem::MockFile());
  NiceMock<MockCluster> cluster;
  std::shared_ptr<MockHostDescription> host(new NiceMock<MockHostDescription>());
  ON_CALL(*host, cluster()).WillByDefault(ReturnRef(cluster));
  NiceMock<MockSystemTimeSource> time_source;

  EXPECT_CALL(log_manager, createAccessLog("foo")).WillOnce(Return(file));
  EventLoggerImpl event_logger(log_manager, "foo", time_source);

  std::string log1;
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"cluster\": "
                           "\"fake_cluster\", \"upstream_ip\": \"tcp://10.0.0.1:443\", \"action\": "
                           "\"eject\", \"type\": \"5xx\"}\n")).WillOnce(SaveArg<0>(&log1));
  event_logger.logEject(host, EjectionType::Consecutive5xx);
  Json::Factory::LoadFromString(log1);

  std::string log2;
  EXPECT_CALL(*file,
              write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"cluster\": \"fake_cluster\", "
                    "\"upstream_ip\": \"tcp://10.0.0.1:443\", \"action\": \"uneject\"}\n"))
      .WillOnce(SaveArg<0>(&log2));
  event_logger.logUneject(host);
  Json::Factory::LoadFromString(log2);
}

} // Outlier
} // Upstream
