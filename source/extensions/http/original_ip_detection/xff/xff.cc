#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "source/common/http/utility.h"
#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

absl::StatusOr<std::unique_ptr<XffIPDetection>> XffIPDetection::create(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config) {

  if (config.has_xff_trusted_cidrs() && config.xff_num_trusted_hops() > 0) {
    return absl::InvalidArgumentError("Cannot set both xff_num_trusted_hops and xff_trusted_cidrs");
  }
  return std::unique_ptr<XffIPDetection>(new XffIPDetection(config));
}

XffIPDetection::XffIPDetection(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(config.xff_num_trusted_hops()),
      skip_xff_append_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, skip_xff_append, true)) {
  if (config.has_xff_trusted_cidrs()) {
    xff_trusted_cidrs_.reserve(config.xff_trusted_cidrs().cidrs().size());
    for (const envoy::config::core::v3::CidrRange& entry : config.xff_trusted_cidrs().cidrs()) {
      absl::StatusOr<Network::Address::CidrRange> cidr_or_error =
          Network::Address::CidrRange::create(entry);
      if (cidr_or_error.ok()) {
        xff_trusted_cidrs_.push_back(cidr_or_error.value());
      }
    }
  }
}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops, bool skip_xff_append)
    : xff_num_trusted_hops_(xff_num_trusted_hops), skip_xff_append_(skip_xff_append) {}

XffIPDetection::XffIPDetection(const std::vector<Network::Address::CidrRange> trusted_cidrs,
                               bool skip_xff_append)
    : xff_num_trusted_hops_(0), xff_trusted_cidrs_(trusted_cidrs),
      skip_xff_append_(skip_xff_append) {}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  if (!xff_trusted_cidrs_.empty()) {
    if (!Envoy::Http::Utility::remoteAddressIsTrustedProxy(*params.downstream_remote_address.get(),
                                                           xff_trusted_cidrs_)) {
      return {nullptr, false, absl::nullopt, false};
    }
    // Check XFF for last IP that isn't in `xff_trusted_cidrs`
    auto ret = Envoy::Http::Utility::getLastNonTrustedAddressFromXFF(params.request_headers,
                                                                     xff_trusted_cidrs_);
    return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, skip_xff_append_};
  }

  auto ret =
      Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, skip_xff_append_};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
