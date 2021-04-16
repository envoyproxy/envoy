#include "ip_detection_extensions.h"

#include "extensions/http/original_ip_detection/custom_header/custom_header.h"
#include "extensions/http/original_ip_detection/xff/xff.h"

namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(hops);
}

Http::OriginalIPDetectionSharedPtr getCustomHeaderExtension(const std::string& header_name) {
  return std::make_shared<
      Extensions::Http::OriginalIPDetection::CustomHeader::CustomHeaderIPDetection>(header_name);
}

Http::OriginalIPDetectionSharedPtr
getCustomHeaderExtension(const std::string& header_name,
                         Http::OriginalIPRejectRequestOptions reject_options) {
  return std::make_shared<
      Extensions::Http::OriginalIPDetection::CustomHeader::CustomHeaderIPDetection>(header_name,
                                                                                    reject_options);
}

} // namespace Envoy
