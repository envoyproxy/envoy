#pragma once

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/http/original_ip_detection.h"

#include "source/common/network/cidr_range.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

/**
 * XFF (x-forwarded-for) IP detection extension.
 */
class XffIPDetection : public Envoy::Http::OriginalIPDetection {
public:
  XffIPDetection(const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config);
  XffIPDetection(uint32_t xff_num_trusted_hops);
  XffIPDetection(Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& xff_trusted_cidrs,
                 bool recurse);

  Envoy::Http::OriginalIPDetectionResult
  detect(Envoy::Http::OriginalIPDetectionParams& params) override;

private:
  const uint32_t xff_num_trusted_hops_;
  std::vector<Network::Address::CidrRange> xff_trusted_cidrs_;
  const bool recurse_;
};

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
