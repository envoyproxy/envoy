#include "test/common/http/custom_header_extension.h"

#include "source/extensions/http/original_ip_detection/custom_header/custom_header.h"

namespace Envoy {

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
