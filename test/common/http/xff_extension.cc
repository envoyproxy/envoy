#include "test/common/http/xff_extension.h"

#include "source/extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(
      hops, skip_xff_append);
}

Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(
      cidrs, skip_xff_append);
}

} // namespace Envoy
