#pragma once

#include "envoy/http/original_ip_detection.h"

// This helper is used to escape namespace pollution issues.
namespace Envoy {

Http::OriginalIPDetectionSharedPtr getXFFExtension(uint32_t hops, bool append_xff);
Http::OriginalIPDetectionSharedPtr getXFFExtension(std::vector<Network::Address::CidrRange> cidrs,
                                                   bool append_xff, bool recurse);

} // namespace Envoy
