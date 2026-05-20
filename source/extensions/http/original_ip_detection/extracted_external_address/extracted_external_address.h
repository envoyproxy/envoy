#pragma once

#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.h"
#include "envoy/http/original_ip_detection.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

// Detects the original downstream remote IP from the Envoy-managed
// ``x-envoy-external-address`` request header populated by an upstream Envoy
// proxy. Intended for interior proxies (sidecars, waypoints, second-tier
// gateways) in hierarchical deployments where the edge proxy has already
// extracted the original client IP and recorded it in
// ``x-envoy-external-address``.
//
// The header is read via the typed
// ``Http::RequestHeaderMap::EnvoyExternalAddress()`` accessor, so the binding
// to the Envoy-managed header is enforced at compile time. When the header is
// present and parses as a valid IP, the address is treated as trusted. When
// the header is absent or unparseable, no detection result is returned and
// the chain may continue.
class ExtractedExternalAddressIPDetection : public Envoy::Http::OriginalIPDetection {
public:
  ExtractedExternalAddressIPDetection(
      const envoy::extensions::http::original_ip_detection::extracted_external_address::v3::
          ExtractedExternalAddressConfig& config);

  Envoy::Http::OriginalIPDetectionResult
  detect(Envoy::Http::OriginalIPDetectionParams& params) override;
};

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
