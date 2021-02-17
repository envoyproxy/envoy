#pragma once

#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/original_ip_detection.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

class CustomHeaderIPDetection : public Http::OriginalIPDetection {
public:
  CustomHeaderIPDetection(
      const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig&
          config);
  CustomHeaderIPDetection(
      const std::string& header_name,
      absl::optional<Http::OriginalIPRejectRequestOptions> reject_options = absl::nullopt);

  Http::OriginalIPDetectionResult detect(Http::OriginalIPDetectionParams& params) override;

private:
  std::string header_name_;
  bool allow_trusted_address_checks_{false};
  absl::optional<Http::OriginalIPRejectRequestOptions> reject_options_{absl::nullopt};
};

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
