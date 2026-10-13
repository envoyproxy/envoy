#pragma once

#include "envoy/http/original_ip_detection.h"

// This helper is used to escape namespace pollution issues.
namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool skip_xff_append);
Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool skip_xff_append);

} // namespace Envoy
