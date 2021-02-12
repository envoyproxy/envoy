#include "extensions/original_ip_detection/custom_header/custom_header.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

CustomHeaderIPDetection::CustomHeaderIPDetection(
    const envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig& config)
    : header_name_(config.header_name()),
      allow_trusted_address_checks_(config.allow_trusted_address_checks()) {}

Http::OriginalIPDetectionResult
CustomHeaderIPDetection::detect(Http::OriginalIPDetectionParams& params) {
  auto hdr = params.request_headers.get(Http::LowerCaseString(header_name_));
  if (hdr.empty()) {
    return {nullptr, false};
  }
  auto header_value = hdr[0]->value().getStringView();
  try {
    return {Network::Utility::parseInternetAddress(std::string(header_value)),
            allow_trusted_address_checks_};
  } catch (const EnvoyException&) {
  }

  return {nullptr, false};
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
