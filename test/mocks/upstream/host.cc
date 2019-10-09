#include "test/mocks/upstream/host.h"

#include "common/network/utility.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace Outlier {

MockDetectorHostMonitor::MockDetectorHostMonitor() = default;
MockDetectorHostMonitor::~MockDetectorHostMonitor() = default;

MockEventLogger::MockEventLogger() = default;
MockEventLogger::~MockEventLogger() = default;

MockDetector::MockDetector() {
  ON_CALL(*this, addChangedStateCb(_)).WillByDefault(Invoke([this](ChangeStateCb cb) -> void {
    callbacks_.push_back(cb);
  }));
}

MockDetector::~MockDetector() = default;

} // namespace Outlier

MockHealthCheckHostMonitor::MockHealthCheckHostMonitor() = default;
MockHealthCheckHostMonitor::~MockHealthCheckHostMonitor() = default;

MockHostDescription::MockHostDescription()
    : address_(Network::Utility::resolveUrl("tcp://10.0.0.1:443")),
      socket_factory_(new testing::NiceMock<Network::MockTransportSocketFactory>) {
  ON_CALL(*this, hostname()).WillByDefault(ReturnRef(hostname_));
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, healthChecker()).WillByDefault(ReturnRef(health_checker_));
  ON_CALL(*this, transportSocketFactory()).WillByDefault(ReturnRef(*socket_factory_));
}

MockHostDescription::~MockHostDescription() = default;

MockHost::MockHost() : socket_factory_(new testing::NiceMock<Network::MockTransportSocketFactory>) {
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, outlierDetector()).WillByDefault(ReturnRef(outlier_detector_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, warmed()).WillByDefault(Return(true));
  ON_CALL(*this, transportSocketFactory()).WillByDefault(ReturnRef(*socket_factory_));
}

MockHost::~MockHost() = default;

} // namespace Upstream
} // namespace Envoy
