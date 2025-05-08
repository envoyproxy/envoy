#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

void ExtMonitorBase::reportResult(bool error) {
  if (error) {
    // Count as error.
    if (onError()) {
      callback_(this);
      // Reaching error was reported via callback.
      // but the host may or may not be ejected based on enforce_ parameter.
      // Reset the monitor's state, so a single new error does not
      // immediately trigger error condition again.
      onReset();
    }
  } else {
    onSuccess();
  }
}

ExtMonitorConfig::ExtMonitorConfig(
    const std::string& name,
    const envoy::extensions::outlier_detection_monitors::common::v3::MonitorCommonSettings&
        common_settings)
    : name_(name), enforce_(common_settings.enforcing().value()),
      enforce_runtime_key_("outlier_detection.enforcing_extension." + name) {}

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
