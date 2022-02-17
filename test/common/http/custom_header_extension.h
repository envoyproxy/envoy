#pragma once

#include "envoy/http/original_ip_detection.h"

// This helper is used to escape namespace pollution issues.
namespace Envoy {

Http::OriginalIPDetectionSharedPtr getCustomHeaderExtension(const std::string& header_name);
Http::OriginalIPDetectionSharedPtr
getCustomHeaderExtension(const std::string& header_name,
                         Http::OriginalIPRejectRequestOptions reject_options);

} // namespace Envoy
