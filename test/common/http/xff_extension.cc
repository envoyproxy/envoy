#include "test/common/http/xff_extension.h"

#include "source/extensions/http/original_ip_detection/xff/xff.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"

namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(
      hops, skip_xff_append);
}

Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append) {
  return std::make_shared<Extensions::Http::OriginalIPDetection::Xff::XffIPDetection>(
      cidrs, skip_xff_append);
}

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append, 
                                                   bool use_remote_address, bool append_x_forwarded_port) {
  // Create config with new use_remote_address parameter
  envoy::extensions::http::original_ip_detection::xff::v3::XffConfig config;
  config.set_xff_num_trusted_hops(hops);
  config.mutable_skip_xff_append()->set_value(skip_xff_append);
  config.mutable_use_remote_address()->set_value(use_remote_address);
  config.set_append_x_forwarded_port(append_x_forwarded_port);
  
  auto result = Extensions::Http::OriginalIPDetection::Xff::XffIPDetection::create(config);
  if (!result.ok()) {
    throw std::runtime_error("Failed to create XFF extension: " + result.status().ToString());
  }
  return std::move(result.value());
}

Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append, bool use_remote_address, 
                                                   bool append_x_forwarded_port) {
  // Create config with CIDR and use_remote_address parameters
  envoy::extensions::http::original_ip_detection::xff::v3::XffConfig config;
  auto* trusted_cidrs = config.mutable_xff_trusted_cidrs();
  for (const auto& cidr : cidrs) {
    auto* entry = trusted_cidrs->add_cidrs();
    entry->set_address_prefix(cidr.ip()->addressAsString());
    entry->mutable_prefix_len()->set_value(cidr.length());
  }
  config.mutable_skip_xff_append()->set_value(skip_xff_append);
  config.mutable_use_remote_address()->set_value(use_remote_address);
  config.set_append_x_forwarded_port(append_x_forwarded_port);
  
  auto result = Extensions::Http::OriginalIPDetection::Xff::XffIPDetection::create(config);
  if (!result.ok()) {
    throw std::runtime_error("Failed to create XFF extension: " + result.status().ToString());
  }
  return std::move(result.value());
}

} // namespace Envoy
