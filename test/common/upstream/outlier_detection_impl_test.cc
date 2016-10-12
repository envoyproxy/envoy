#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;

namespace Upstream {

TEST(OutlierDetectorImplFactoryTest, NoDetector) {
  Json::StringLoader loader("{}");
  MockCluster cluster;
  Event::MockDispatcher dispatcher;
  EXPECT_EQ(nullptr, OutlierDetectorImplFactory::createForCluster(cluster, loader, dispatcher));
}

TEST(OutlierDetectorImplFactoryTest, Detector) {
  std::string json = R"EOF(
  {
    "outlier_detection": {}
  }
  )EOF";

  Json::StringLoader loader(json);
  NiceMock<MockCluster> cluster;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_NE(nullptr, OutlierDetectorImplFactory::createForCluster(cluster, loader, dispatcher));
}

TEST(OutlierDetectorImplTest, Callbacks) {
  NiceMock<MockCluster> cluster;
  Event::MockDispatcher dispatcher;

  EXPECT_CALL(cluster, addMemberUpdateCb(_));
  cluster.hosts_ = {HostPtr{new HostImpl(cluster, "tcp://127.0.0.1:80", false, 1, "")}};
  OutlierDetectorImpl detector(cluster, dispatcher);

  // Set up callback. Will replace later with real test when we have real functionality.
  detector.addChangedStateCb([](HostPtr) -> void {});

  cluster.hosts_.push_back(HostPtr{new HostImpl(cluster, "tcp://127.0.0.1:81", false, 1, "")});
  cluster.runCallbacks({cluster.hosts_[1]}, {});

  // Trivial call through tests to be replaced later with real functionality.
  cluster.hosts_[0]->outlierDetector().putHttpResponseCode(200);
  cluster.hosts_[0]->outlierDetector().putResponseTime(std::chrono::milliseconds(5));

  cluster.runCallbacks({}, cluster.hosts_);
}

} // Upstream
