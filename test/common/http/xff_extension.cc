#include "test/common/http/xff_extension.h"

#include "source/extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(hops);
}

} // namespace Envoy
