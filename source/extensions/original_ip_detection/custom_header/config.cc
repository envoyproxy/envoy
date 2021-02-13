#include "extensions/original_ip_detection/custom_header/config.h"

#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.validate.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "extensions/original_ip_detection/custom_header/custom_header.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

Http::OriginalIPDetectionSharedPtr
CustomHeaderIPDetectionFactory::createExtension(const Protobuf::Message& message) const {
  return std::make_shared<CustomHeaderIPDetection>(
      dynamic_cast<
          const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig&>(
          message));
}

REGISTER_FACTORY(CustomHeaderIPDetectionFactory, Http::OriginalIPDetectionFactory);

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
