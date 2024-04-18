#pragma once

#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/original_ip_detection.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

/**
 * Custom header IP detection extension.
 */
class CustomHeaderIPDetection : public Envoy::Http::OriginalIPDetection {
public:
  CustomHeaderIPDetection(
      const envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig&
          config);
  CustomHeaderIPDetection(
      const std::string& header_name,
      absl::optional<Envoy::Http::OriginalIPRejectRequestOptions> reject_options = absl::nullopt);

  Envoy::Http::OriginalIPDetectionResult
  detect(Envoy::Http::OriginalIPDetectionParams& params) override;

private:
  static Envoy::Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Envoy::Http::Code>(status);
    if (code >= Envoy::Http::Code::BadRequest &&
        code <= Envoy::Http::Code::NetworkAuthenticationRequired) {
      return code;
    }
    return Envoy::Http::Code::Forbidden;
  }

  Envoy::Http::LowerCaseString header_name_;
  bool allow_trusted_address_checks_{false};
  absl::optional<Envoy::Http::OriginalIPRejectRequestOptions> reject_options_{absl::nullopt};
};

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
