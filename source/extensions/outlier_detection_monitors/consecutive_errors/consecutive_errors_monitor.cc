#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.h"
#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

// Possibly should be moved to another namespace and into another directory
// source/extensions/outlier_detection/consecutive_errors/

namespace Envoy {
namespace Extensions {
namespace Outlier {

class ConsecutiveErrorsMonitorFactory
    : public MonitorFactoryBase<envoy::extensions::outlier_detection_monitors::consecutive_errors::
                                    v3::ConsecutiveErrors> {
public:
  ConsecutiveErrorsMonitorFactory()
      : MonitorFactoryBase("envoy.outlier_detection_monitors.consecutive_errors") {}

private:
  ODMonitorPtr createMonitorFromProtoTyped(const envoy::extensions::outlier_detection_monitors::
                                               consecutive_errors::v3::ConsecutiveErrors&,
                                           MonitorFactoryContext&) override {
    // TODO: Create consecutive errors type of monitor.
    ASSERT(false);
    return std::make_unique<ODMonitor>();
  }
};

REGISTER_FACTORY(ConsecutiveErrorsMonitorFactory, MonitorFactory);

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
