#include "source/extensions/outlier_detection_monitors/consecutive_errors/consecutive_errors_monitor.h"

#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.h"
#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.validate.h"
#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.h"
#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.validate.h"
#include "envoy/registry/registry.h"

// Possibly should be moved to another namespace and into another directory
// source/extensions/outlier_detection/consecutive_errors/

namespace Envoy {
namespace Extensions {
namespace Outlier {

bool ConsecutiveErrorsMonitor::onError() {
  if (counter_ < max_) {
    counter_++;
  }

  return (counter_ == max_);
}

void ConsecutiveErrorsMonitor::onSuccess() {
  // start counting from zero again.
  counter_ = 0;
}

void ConsecutiveErrorsMonitor::onReset() { counter_ = 0; }

class ConsecutiveErrorsMonitorFactory
    : public MonitorFactoryBase<envoy::extensions::outlier_detection_monitors::consecutive_errors::
                                    v3::ConsecutiveErrors> {
public:
  ConsecutiveErrorsMonitorFactory()
      : MonitorFactoryBase("envoy.outlier_detection_monitors.consecutive_errors") {}

private:
  MonitorPtr createMonitorFromProtoTyped(const envoy::extensions::outlier_detection_monitors::
                                             consecutive_errors::v3::ConsecutiveErrors& config,
                                         MonitorFactoryContext&) override {
    // TODO: Create consecutive errors type of monitor.
    auto monitor = std::make_unique<ConsecutiveErrorsMonitor>(
        config.name(), config.enforcing().value(),
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, threshold, 3));
    processBucketsConfig(*monitor, config.error_buckets());

    return monitor;
  }
};

REGISTER_FACTORY(ConsecutiveErrorsMonitorFactory, MonitorFactory);

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
