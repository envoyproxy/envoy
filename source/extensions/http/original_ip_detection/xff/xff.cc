#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

XffIPDetection::XffIPDetection(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(config.xff_num_trusted_hops()) {}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops)
    : xff_num_trusted_hops_(xff_num_trusted_hops) {}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  auto ret =
      Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
