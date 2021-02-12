#pragma once

#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/http/original_ip_detection.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

class XffIPDetection : public Http::OriginalIPDetection {
public:
  XffIPDetection(const envoy::extensions::original_ip_detection::xff::v3::XffConfig& config);
  XffIPDetection(uint32_t xff_num_trusted_hops);

  Http::OriginalIPDetectionResult detect(Http::OriginalIPDetectionParams& params) override;

private:
  const uint32_t xff_num_trusted_hops_;
};

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
