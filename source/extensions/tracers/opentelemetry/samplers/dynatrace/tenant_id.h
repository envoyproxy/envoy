#pragma once

#include <cstdint>
#include <string>

#include "absl/strings/str_cat.h"
#include "openssl/md5.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

/**
 * @brief Calculates a Dynatrace specific tenant id which is used in the Dynatrace tag added to the
 * tracestate header.
 *
 * @param file_name The tenant as received via config file
 * @return the tenant id as Hex
 */
absl::Hex calculateTenantId(std::string tenant_uuid) {
  if (tenant_uuid.empty()) {
    return absl::Hex(0);
  }

  for (char& c : tenant_uuid) {
    if (c & 0x80) {
      c = 0x3f; // '?'
    }
  }

  uint8_t digest[16];
  MD5(reinterpret_cast<const uint8_t*>(tenant_uuid.data()), tenant_uuid.size(), digest);

  int32_t hash = 0;
  for (int i = 0; i < 16; i++) {
    const int shift_for_target_byte = (3 - (i % 4)) * 8;
    // 24, 16, 8, 0 respectively
    hash ^=
        (static_cast<int>(digest[i]) << shift_for_target_byte) & (0xff << shift_for_target_byte);
  }
  return absl::Hex(hash);
}
} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
