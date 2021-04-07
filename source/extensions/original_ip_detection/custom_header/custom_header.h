#pragma once

#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/original_ip_detection.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

/**
 * Custom header IP detection extension.
 */
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
  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::BadRequest && code <= Http::Code::NetworkAuthenticationRequired) {
      return code;
    }
    return Http::Code::Forbidden;
  }

  Http::LowerCaseString header_name_;
  bool allow_trusted_address_checks_{false};
  absl::optional<Http::OriginalIPRejectRequestOptions> reject_options_{absl::nullopt};
};

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
