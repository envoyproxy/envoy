#pragma once

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/network/cidr_range.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/network/utility.h"
#include "source/common/http/utility.h"
#include "source/common/http/conn_manager_config.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

/**
 * XFF (x-forwarded-for) IP detection extension.
 */
class XffIPDetection : public Envoy::Http::OriginalIPDetection,
                       Logger::Loggable<Logger::Id::config> {
public:
  static absl::StatusOr<std::unique_ptr<XffIPDetection>>
  create(const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config);

  XffIPDetection(uint32_t xff_num_trusted_hops, bool skip_xff_append);
  XffIPDetection(const std::vector<Network::Address::CidrRange> xff_trusted_cidrs,
                 bool skip_xff_append);

  Envoy::Http::OriginalIPDetectionResult
  detect(Envoy::Http::OriginalIPDetectionParams& params) override;

protected:
  XffIPDetection(const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config);

private:
  // Helper method to append XFF header
  void appendXff(Envoy::Http::RequestHeaderMap& request_headers, 
                 const Network::Address::Instance& remote_address);

  // Helper method to handle header manipulation based on use_remote_address setting
  Envoy::Http::OriginalIPDetectionResult 
  handleUseRemoteAddressTrue(Envoy::Http::OriginalIPDetectionParams& params);
  
  Envoy::Http::OriginalIPDetectionResult 
  handleUseRemoteAddressFalse(Envoy::Http::OriginalIPDetectionParams& params);

  // Helper method to determine if an address is internal (loopback)
  bool isLoopbackAddress(const Network::Address::Instance& address);

  const uint32_t xff_num_trusted_hops_;
  std::vector<Network::Address::CidrRange> xff_trusted_cidrs_;
  const bool skip_xff_append_;
  const bool use_remote_address_;
  const bool append_x_forwarded_port_;
};

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
