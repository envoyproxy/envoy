#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "source/common/http/utility.h"
#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

XffIPDetection::XffIPDetection(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(config.xff_num_trusted_hops()), append_xff_(config.append_xff()),
      recurse_(config.xff_trusted_cidrs().recurse()) {
  if (config.has_xff_trusted_cidrs() && config.xff_num_trusted_hops() > 0) {
    throw EnvoyException("Cannot set both xff_num_trusted_hops and xff_trusted_cidrs");
  }
  xff_trusted_cidrs_.reserve(config.xff_trusted_cidrs().cidrs().size());
  for (const envoy::config::core::v3::CidrRange& entry : config.xff_trusted_cidrs().cidrs()) {
    Network::Address::CidrRange cidr = Network::Address::CidrRange::create(entry);
    xff_trusted_cidrs_.push_back(cidr);
  }
}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops, bool append_xff)
    : xff_num_trusted_hops_(xff_num_trusted_hops), append_xff_(append_xff), recurse_(false) {}

XffIPDetection::XffIPDetection(const std::vector<Network::Address::CidrRange> trusted_cidrs,
                               bool append_xff, bool recurse)
    : xff_num_trusted_hops_(0), xff_trusted_cidrs_(trusted_cidrs), append_xff_(append_xff),
      recurse_(recurse) {}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  if (!xff_trusted_cidrs_.empty()) {
    if (!Envoy::Http::Utility::remoteAddressIsTrustedProxy(params.downstream_remote_address,
                                                           xff_trusted_cidrs_)) {
      return {nullptr, false, absl::nullopt, false};
    }
    if (recurse_) {
      // Check XFF for last IP that isn't in `xff_trusted_cidrs`
      auto ret = Envoy::Http::Utility::getLastNonTrustedAddressFromXFF(params.request_headers,
                                                                       xff_trusted_cidrs_);
      return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, append_xff_};
    }
  }

  auto ret =
      Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, append_xff_};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
