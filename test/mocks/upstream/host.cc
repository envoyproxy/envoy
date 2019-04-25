#include "test/mocks/upstream/host.h"

#include "common/network/utility.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace Outlier {

MockDetectorHostMonitor::MockDetectorHostMonitor() {}
MockDetectorHostMonitor::~MockDetectorHostMonitor() {}

MockEventLogger::MockEventLogger() {}
MockEventLogger::~MockEventLogger() {}

MockDetector::MockDetector() {
  ON_CALL(*this, addChangedStateCb(_)).WillByDefault(Invoke([this](ChangeStateCb cb) -> void {
    callbacks_.push_back(cb);
  }));
}

MockDetector::~MockDetector() {}

} // namespace Outlier

MockHealthCheckHostMonitor::MockHealthCheckHostMonitor() {}
MockHealthCheckHostMonitor::~MockHealthCheckHostMonitor() {}

MockHostDescription::MockHostDescription()
    : address_(Network::Utility::resolveUrl("tcp://10.0.0.1:443")) {
  ON_CALL(*this, hostname()).WillByDefault(ReturnRef(hostname_));
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, healthChecker()).WillByDefault(ReturnRef(health_checker_));
}

MockHostDescription::~MockHostDescription() {}

MockHost::MockHost() {
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
}

MockHost::~MockHost() {}

} // namespace Upstream
} // namespace Envoy
