#include "extensions/original_ip_detection/xff/xff.h"

#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

XffIPDetection::XffIPDetection(
    const envoy::extensions::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(config.xff_num_trusted_hops()) {}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops)
    : xff_num_trusted_hops_(xff_num_trusted_hops) {}

Http::OriginalIPDetectionResult XffIPDetection::detect(Http::OriginalIPDetectionParams& params) {
  auto ret = Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.single_address_, absl::nullopt};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
