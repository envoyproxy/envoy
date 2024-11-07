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
    : public ExtMonitorFactoryBase<envoy::extensions::outlier_detection_monitors::
                                       consecutive_errors::v3::ConsecutiveErrors> {
public:
  ConsecutiveErrorsMonitorFactory()
      : ExtMonitorFactoryBase("envoy.outlier_detection_monitors.consecutive_errors") {}

private:
  ExtMonitorCreateFn
  createMonitorFromProtoTyped(const std::string& monitor_name,
                              const envoy::extensions::outlier_detection_monitors::
                                  consecutive_errors::v3::ConsecutiveErrors& config,
                              ExtMonitorFactoryContext&) override {
    auto ext_config = std::make_shared<ExtMonitorConfig>(monitor_name, config.errors());
    uint32_t max = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, threshold, 3);
    return [ext_config, max]() {
      auto monitor = std::make_unique<ConsecutiveErrorsMonitor>(ext_config, max);

      return monitor;
    };
  }
};

REGISTER_FACTORY(ConsecutiveErrorsMonitorFactory, ExtMonitorFactory);

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
