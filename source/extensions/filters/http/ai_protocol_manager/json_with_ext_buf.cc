#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include <cstdint>
#include <vector>

#include "source/common/common/safe_memcpy.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

nlohmann::json JsonWithExtBuf::makeExternalRef(const ExternalRef& ref) {
  std::vector<std::uint8_t> payload(kExternalRefBytes, 0);
  safeMemcpyUnsafeDst(payload.data(), &ref);
  return nlohmann::json::binary(std::move(payload), kExternalRefSubtype);
}

bool JsonWithExtBuf::isExternalRef(const nlohmann::json& value) {
  if (!value.is_binary()) {
    return false;
  }
  const auto& payload = value.get_binary();
  return payload.has_subtype() && payload.subtype() == kExternalRefSubtype;
}

absl::StatusOr<JsonWithExtBuf::ExternalRef>
JsonWithExtBuf::externalRef(const nlohmann::json& value) {
  if (!isExternalRef(value)) {
    return absl::InvalidArgumentError("not an external buffer reference");
  }
  const auto& payload = value.get_binary();
  if (payload.size() != kExternalRefBytes) {
    return absl::InvalidArgumentError(absl::StrCat("external buffer reference is ", payload.size(),
                                                   " bytes, expected ", kExternalRefBytes));
  }
  ExternalRef ref;
  safeMemcpyUnsafeSrc(&ref, payload.data());
  return ref;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
