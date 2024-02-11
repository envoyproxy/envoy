#include "source/extensions/http/original_ip_detection/custom_header/custom_header.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig&
        config)
    : header_name_(config.header_name()),
      allow_trusted_address_checks_(config.allow_extension_to_set_address_as_trusted()) {
  if (config.has_reject_with_status()) {
    const auto reject_code = toErrorCode(config.reject_with_status().code());
    reject_options_ = {reject_code, ""};
  }
}

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const std::string& header_name,
    absl::optional<Envoy::Http::OriginalIPRejectRequestOptions> reject_options)
    : header_name_(header_name), reject_options_(reject_options) {}

Envoy::Http::OriginalIPDetectionResult
CustomHeaderIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  auto hdr = params.request_headers.get(header_name_);
  if (hdr.empty()) {
    return {nullptr, false, reject_options_, false};
  }

  auto header_value = hdr[0]->value().getStringView();
  auto addr = Network::Utility::parseInternetAddressNoThrow(std::string(header_value));
  if (addr) {
    return {addr, allow_trusted_address_checks_, absl::nullopt, false};
  }

  return {nullptr, false, reject_options_, false};
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
