#include "extensions/original_ip_detection/custom_header/custom_header.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig& config)
    : header_name_(config.header_name()),
      allow_trusted_address_checks_(config.allow_extension_to_set_address_as_trusted()) {
  if (config.has_reject_options()) {
    const auto reject_code = toErrorCode(config.reject_options().status_on_error().code());
    reject_options_ = {reject_code, config.reject_options().body_on_error()};
  }
}

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const std::string& header_name,
    absl::optional<Http::OriginalIPRejectRequestOptions> reject_options)
    : header_name_(header_name), reject_options_(reject_options) {}

Http::OriginalIPDetectionResult
CustomHeaderIPDetection::detect(Http::OriginalIPDetectionParams& params) {
  auto hdr = params.request_headers.get(header_name_);
  if (hdr.empty()) {
    return {nullptr, false, reject_options_};
  }

  auto header_value = hdr[0]->value().getStringView();
  auto addr = Network::Utility::parseInternetAddressNoThrow(std::string(header_value));
  if (addr) {
    return {addr, allow_trusted_address_checks_, absl::nullopt};
  }

  return {nullptr, false, reject_options_};
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
