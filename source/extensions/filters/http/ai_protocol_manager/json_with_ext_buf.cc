#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// Little-endian encode/decode helpers. Explicit shifts rather than a memcpy of
// the struct so the encoding is fixed by this code and not by the host ABI.
void appendLe64(nlohmann::json::binary_t& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
  }
}

std::uint64_t readLe64(const nlohmann::json::binary_t& in, std::size_t at) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(in[at + i]) << (8 * i);
  }
  return value;
}

} // namespace

nlohmann::json JsonWithExtBuf::makeExternalRef(const ExternalRef& ref) {
  nlohmann::json::binary_t payload;
  payload.reserve(kExternalRefBytes);
  appendLe64(payload, ref.offset);
  appendLe64(payload, ref.length);
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
  return ExternalRef{readLe64(payload, 0), readLe64(payload, 8)};
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
