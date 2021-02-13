#include "extensions/original_ip_detection/xff/config.h"

#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.validate.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "extensions/original_ip_detection/xff/xff.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

Http::OriginalIPDetectionSharedPtr
XffIPDetectionFactory::createExtension(const Protobuf::Message& message) const {
  return std::make_shared<XffIPDetection>(
      dynamic_cast<const envoy::extensions::original_ip_detection::xff::v3::XffConfig&>(message));
}

REGISTER_FACTORY(XffIPDetectionFactory, Http::OriginalIPDetectionFactory);

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
