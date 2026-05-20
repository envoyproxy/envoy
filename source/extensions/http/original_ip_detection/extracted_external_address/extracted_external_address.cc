#include "source/extensions/http/original_ip_detection/extracted_external_address/extracted_external_address.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

ExtractedExternalAddressIPDetection::ExtractedExternalAddressIPDetection(
    const envoy::extensions::http::original_ip_detection::extracted_external_address::v3::
        ExtractedExternalAddressConfig&) {}

Envoy::Http::OriginalIPDetectionResult
ExtractedExternalAddressIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  // ``skip_xff_append`` is set to ``true`` on every return path. An interior
  // proxy reading an upstream-resolved ``x-envoy-external-address`` should not
  // append its own connection peer to ``X-Forwarded-For``: the upstream Envoy
  // already wrote the canonical XFF chain. Appending here would inject the
  // intermediate hop's IP into XFF, which carries no useful topology
  // information for downstream consumers and diverges from how the
  // ``custom_header`` sibling handles the same situation
  // (see ``custom_header.cc``, which also pins ``skip_xff_append=true``
  // unconditionally per Envoy issue #31831).
  constexpr bool skip_xff_append = true;

  // Reading via the typed accessor binds this extension to the Envoy-managed
  // ``x-envoy-external-address`` header at compile time. A future rename of
  // ``RequestHeaderMap::EnvoyExternalAddress()`` becomes a build break here
  // rather than a silent behavioral regression.
  const auto* header = params.request_headers.EnvoyExternalAddress();
  if (header == nullptr) {
    return {nullptr, false, absl::nullopt, skip_xff_append};
  }

  const auto value = header->value().getStringView();
  auto address = Network::Utility::parseInternetAddressNoThrow(std::string(value));
  if (address == nullptr) {
    return {nullptr, false, absl::nullopt, skip_xff_append};
  }

  // The upstream Envoy already resolved this address as the trusted external
  // client IP. Same trust contract as any other reader of
  // ``x-envoy-external-address`` on an interior proxy.
  return {address, /*allow_trusted_address_checks=*/true, absl::nullopt, skip_xff_append};
}

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
