#pragma once

#include "envoy/http/original_ip_detection.h"

// This helper is used to escape namespace pollution issues.
namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append);
Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append);
// Helper for creating XFF extension with use_remote_address parameter
Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append, 
                                                   bool use_remote_address, bool append_x_forwarded_port = false);
// Helper for creating XFF extension with CIDR and use_remote_address parameter  
Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append, bool use_remote_address, 
                                                   bool append_x_forwarded_port = false);

} // namespace Envoy
