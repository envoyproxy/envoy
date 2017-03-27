#include "envoy/common/optional.h"
#include "envoy/common/time.h"

#include "common/network/utility.h"
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
  EXPECT_EQ(nullptr,
            DetectorImplFactory::createForCluster(cluster, *loader, dispatcher, runtime, nullptr));
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
  EXPECT_NE(nullptr,
            DetectorImplFactory::createForCluster(cluster, *loader, dispatcher, runtime, nullptr));
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
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* interval_timer_ = new Event::MockTimer(&dispatcher_);
  CallbackChecker checker_;
  MockSystemTimeSource time_source_;
  std::shared_ptr<MockEventLogger> event_logger_{new MockEventLogger()};
  Json::ObjectPtr loader_ = Json::Factory::LoadFromString("{}");
};

TEST_F(OutlierDetectorImplTest, DetectorStaticConfig) {
  std::string json = R"EOF(
  {
    "interval_ms" : 100,
    "base_ejection_time_ms" : 10000,
    "consecutive_5xx" : 10,
    "max_ejection_percent" : 50,
    "enforcing_consecutive_5xx" : 10,
    "enforcing_success_rate": 20
  }
  )EOF";

  Json::ObjectPtr custom_config = Json::Factory::LoadFromString(json);
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(100)));
  std::shared_ptr<DetectorImpl> detector(DetectorImpl::create(
      cluster_, *custom_config, dispatcher_, runtime_, time_source_, event_logger_));

  EXPECT_EQ(100UL, detector->config().intervalMs());
  EXPECT_EQ(10000UL, detector->config().baseEjectionTimeMs());
  EXPECT_EQ(10UL, detector->config().consecutive5xx());
  EXPECT_EQ(50UL, detector->config().maxEjectionPercent());
  EXPECT_EQ(10UL, detector->config().enforcingConsecutive5xx());
  EXPECT_EQ(20UL, detector->config().enforcingSuccessRate());
}

TEST_F(OutlierDetectorImplTest, DestroyWithActive) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  detector.reset();

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, DestroyHostInUse) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  detector.reset();

  loadRq(cluster_.hosts_[0], 5, 503);
}

TEST_F(OutlierDetectorImplTest, BasicFlow5xx) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  cluster_.hosts_.push_back(HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:81"), false, 1, "")});
  cluster_.runCallbacks({cluster_.hosts_[1]}, {});

  // Cause a consecutive 5xx error.
  loadRq(cluster_.hosts_[0], 1, 503);
  loadRq(cluster_.hosts_[0], 1, 200);
  cluster_.hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

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

  // Eject host again to cause an ejection after an unejection has taken place
  loadRq(cluster_.hosts_[0], 1, 503);
  loadRq(cluster_.hosts_[0], 1, 200);
  cluster_.hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));
  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(40000))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  cluster_.runCallbacks({}, cluster_.hosts_);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(2UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx")
                     .value());
}

TEST_F(OutlierDetectorImplTest, BasicFlowSuccessRate) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {
      HostSharedPtr{new HostImpl(cluster_.info_, "",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")},
      HostSharedPtr{new HostImpl(cluster_.info_, "",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:81"), false, 1, "")},
      HostSharedPtr{new HostImpl(cluster_.info_, "",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:82"), false, 1, "")},
      HostSharedPtr{new HostImpl(cluster_.info_, "",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:83"), false, 1, "")},
      HostSharedPtr{new HostImpl(
          cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:84"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Turn off 5xx detection to test SR detection in isolation.
  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
      .WillByDefault(Return(false));
  // Expect non-enforcing logging
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[4]),
                       EjectionType::Consecutive5xx, false)).Times(2);

  // Cause a consecutive SR error on one host. First have 4 of the hosts have perfect SR.
  loadRq(cluster_.hosts_, 200, 200);
  loadRq(cluster_.hosts_[4], 200, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .Times(2)
      .WillRepeatedly(Return(SystemTime(std::chrono::milliseconds(10000))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[4]),
                       EjectionType::SuccessRate, true));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_TRUE(cluster_.hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that doesn't bring the host back in.
  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(19999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_TRUE(cluster_.hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Interval that does bring the host back in.
  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(50001))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[4]));
  EXPECT_CALL(*event_logger_,
              logUneject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[4])));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_FALSE(cluster_.hosts_[4]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  // Give 4 hosts enough request volume but not to the 5th. Should not cause an ejection.
  loadRq(cluster_.hosts_, 25, 200);
  loadRq(cluster_.hosts_[4], 25, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(60001))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, RemoveWhileEjected) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  std::vector<HostSharedPtr> old_hosts = std::move(cluster_.hosts_);
  cluster_.runCallbacks({}, old_hosts);

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(9999))));
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  interval_timer_->callback_();
}

TEST_F(OutlierDetectorImplTest, Overflow) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {
      HostSharedPtr{new HostImpl(cluster_.info_, "",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")},
      HostSharedPtr{new HostImpl(
          cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:81"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  ON_CALL(runtime_.snapshot_, getInteger("outlier_detection.max_ejection_percent", _))
      .WillByDefault(Return(1));

  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  cluster_.hosts_[0]->outlierDetector().putHttpResponseCode(503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  loadRq(cluster_.hosts_[1], 5, 503);
  EXPECT_FALSE(cluster_.hosts_[1]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL,
            cluster_.info_->stats_store_.counter("outlier_detection.ejections_overflow").value());
}

TEST_F(OutlierDetectorImplTest, NotEnforcing) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  ON_CALL(runtime_.snapshot_, featureEnabled("outlier_detection.enforcing_consecutive_5xx", 100))
      .WillByDefault(Return(false));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_FALSE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_total").value());
  EXPECT_EQ(1UL, cluster_.info_->stats_store_.counter("outlier_detection.ejections_consecutive_5xx")
                     .value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadRemoveRace) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(cluster_.hosts_[0], 1, 503);

  // Remove before the cross thread event comes in.
  std::vector<HostSharedPtr> old_hosts = std::move(cluster_.hosts_);
  cluster_.runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadDestroyRace) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(cluster_.hosts_[0], 1, 503);

  // Destroy before the cross thread event comes in.
  std::weak_ptr<DetectorImpl> weak_detector = detector;
  detector.reset();
  EXPECT_EQ(nullptr, weak_detector.lock());
  post_cb();

  EXPECT_EQ(0UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, CrossThreadFailRace) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  loadRq(cluster_.hosts_[0], 4, 503);

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  loadRq(cluster_.hosts_[0], 1, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));

  // Fire the post callback twice. This should only result in a single ejection.
  post_cb();
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  post_cb();

  EXPECT_EQ(1UL, cluster_.info_->stats_store_.gauge("outlier_detection.ejections_active").value());
}

TEST_F(OutlierDetectorImplTest, Consecutive5xxAlreadyEjected) {
  EXPECT_CALL(cluster_, addMemberUpdateCb(_));
  cluster_.hosts_ = {HostSharedPtr{new HostImpl(
      cluster_.info_, "", Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, "")}};
  EXPECT_CALL(*interval_timer_, enableTimer(std::chrono::milliseconds(10000)));
  std::shared_ptr<DetectorImpl> detector(
      DetectorImpl::create(cluster_, *loader_, dispatcher_, runtime_, time_source_, event_logger_));
  detector->addChangedStateCb([&](HostSharedPtr host) -> void { checker_.check(host); });

  // Cause a consecutive 5xx error.
  loadRq(cluster_.hosts_[0], 4, 503);

  EXPECT_CALL(time_source_, currentSystemTime())
      .WillOnce(Return(SystemTime(std::chrono::milliseconds(0))));
  EXPECT_CALL(checker_, check(cluster_.hosts_[0]));
  EXPECT_CALL(*event_logger_,
              logEject(std::static_pointer_cast<const HostDescription>(cluster_.hosts_[0]),
                       EjectionType::Consecutive5xx, true));
  loadRq(cluster_.hosts_[0], 1, 503);
  EXPECT_TRUE(cluster_.hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));

  // Cause another consecutive 5xx error.
  loadRq(cluster_.hosts_[0], 1, 200);
  loadRq(cluster_.hosts_[0], 5, 503);
}

TEST(DetectorHostSinkNullImplTest, All) {
  DetectorHostSinkNullImpl null_sink;

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
  Optional<SystemTime> time;

  EXPECT_CALL(log_manager, createAccessLog("foo")).WillOnce(Return(file));
  EventLoggerImpl event_logger(log_manager, "foo", time_source);

  std::string log1;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(time));
  EXPECT_CALL(
      *file,
      write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
            "\"-1\", \"cluster\": "
            "\"fake_cluster\", \"upstream_url\": \"10.0.0.1:443\", \"action\": "
            "\"eject\", \"type\": \"5xx\", \"num_ejections\": \"0\", \"enforced\": \"true\"}\n"))
      .WillOnce(SaveArg<0>(&log1));
  event_logger.logEject(host, EjectionType::Consecutive5xx, true);
  Json::Factory::LoadFromString(log1);

  std::string log2;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"-1\", \"cluster\": \"fake_cluster\", "
                           "\"upstream_url\": \"10.0.0.1:443\", \"action\": \"uneject\", "
                           "\"num_ejections\": 0}\n")).WillOnce(SaveArg<0>(&log2));
  event_logger.logUneject(host);
  Json::Factory::LoadFromString(log2);

  // now test with time since last action.
  time.value(time_source.currentSystemTime() - std::chrono::seconds(30));

  std::string log3;
  EXPECT_CALL(host->outlier_detector_, lastUnejectionTime()).WillOnce(ReturnRef(time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"30\", \"cluster\": "
                           "\"fake_cluster\", \"upstream_url\": \"10.0.0.1:443\", \"action\": "
                           "\"eject\", \"type\": \"SuccessRate\", \"num_ejections\": \"0\", "
                           "\"enforced\": \"false\"}\n")).WillOnce(SaveArg<0>(&log3));
  event_logger.logEject(host, EjectionType::SuccessRate, false);
  Json::Factory::LoadFromString(log3);

  std::string log4;
  EXPECT_CALL(host->outlier_detector_, lastEjectionTime()).WillOnce(ReturnRef(time));
  EXPECT_CALL(*file, write("{\"time\": \"1970-01-01T00:00:00.000Z\", \"secs_since_last_action\": "
                           "\"30\", \"cluster\": \"fake_cluster\", "
                           "\"upstream_url\": \"10.0.0.1:443\", \"action\": \"uneject\", "
                           "\"num_ejections\": 0}\n")).WillOnce(SaveArg<0>(&log4));
  event_logger.logUneject(host);
  Json::Factory::LoadFromString(log4);
}

TEST(OutlierUtility, SRThreshold) {
  std::vector<HostSuccessRatePair> data = {
      HostSuccessRatePair(nullptr, 50),  HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100), HostSuccessRatePair(nullptr, 100),
      HostSuccessRatePair(nullptr, 100),
  };
  double sum = 450;
  double average;

  EXPECT_EQ(Utility::successRateEjectionThreshold(sum, data, average), 52);
  EXPECT_EQ(90.0, average);
}

} // Outlier
} // Upstream
