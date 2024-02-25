#include "test/common/http/xff_extension.h"

#include "source/extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool append_xff) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(hops,
                                                                                      append_xff);
}

Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool append_xff, bool recurse) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(
      cidrs, append_xff, recurse);
}

} // namespace Envoy
