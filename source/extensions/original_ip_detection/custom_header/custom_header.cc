#include "extensions/original_ip_detection/custom_header/custom_header.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig& config)
    : header_name_(config.header_name()),
      allow_trusted_address_checks_(config.common_config().allow_trusted_address_checks()) {

  if (config.has_common_config() && config.common_config().reject_request_if_detection_fails()) {
    Http::Code reject_code = Http::Code::Forbidden;

    if (config.common_config().has_status_on_error()) {
      reject_code = static_cast<Http::Code>(config.common_config().status_on_error().code());
    }

    reject_options_ = {reject_code, config.common_config().body_on_error(),
                       config.common_config().details_on_error()};
  }
}

Http::OriginalIPDetectionResult
CustomHeaderIPDetection::detect(Http::OriginalIPDetectionParams& params) {
  auto hdr = params.request_headers.get(Http::LowerCaseString(header_name_));
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
