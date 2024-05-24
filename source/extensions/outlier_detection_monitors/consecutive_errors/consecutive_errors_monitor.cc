#include "source/extensions/outlier_detection_monitors/consecutive_errors/consecutive_errors_monitor.h"

#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.h"
#include "envoy/extensions/outlier_detection_monitors/consecutive_errors/v3/consecutive_errors.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

bool ConsecutiveErrorsMonitor::onError() {
  uint32_t expected_count = counter_.load();

  // no-op. Just keep executing compare_exchange_strong until threads synchronize.
  do {
    ;
  } while (!counter_.compare_exchange_strong(expected_count, expected_count + 1));

  // The counter_ value may go above max_, but only one thread will see
  // that counter_ reached max_ and will report it.
  return ((expected_count + 1) == max_);
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
  MonitorPtr createMonitorFromProtoTyped(const std::string& monitor_name,
                                         const envoy::extensions::outlier_detection_monitors::
                                             consecutive_errors::v3::ConsecutiveErrors& config,
                                         MonitorFactoryContext&) override {
    auto monitor = std::make_unique<ConsecutiveErrorsMonitor>(
        monitor_name, config.enforcing().value(),
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, threshold, 3));
    monitor->processBucketsConfig(config.error_buckets());

    return monitor;
  }
};

REGISTER_FACTORY(ConsecutiveErrorsMonitorFactory, MonitorFactory);

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
