#include "common/stream_info/utility.h"

#include <string>
#include <type_traits>

namespace Envoy {
namespace StreamInfo {

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  absl::InlinedVector<absl::string_view,
                      std::numeric_limits<std::underlying_type_t<ResponseFlag>>::digits>
      flag_strings;
  for (const auto& [flag_string, flag] : ALL_RESPONSE_STRING_FLAGS) {
    if (stream_info.hasResponseFlag(flag)) {
      flag_strings.push_back(flag_string);
    }
  }
  if (flag_strings.empty()) {
    return std::string(NONE);
  }
  return absl::StrJoin(flag_strings, ",");
}

absl::flat_hash_map<std::string, ResponseFlag> ResponseFlagUtils::getFlagMap() {
  static_assert(ResponseFlag::LastFlag == 0x1000000, "A flag has been added. Fix this code.");
  absl::flat_hash_map<std::string, ResponseFlag> res;
  for (auto [str, flag] : ResponseFlagUtils::ALL_RESPONSE_STRING_FLAGS) {
    res.emplace(str, flag);
  }
  return res;
}

absl::optional<ResponseFlag> ResponseFlagUtils::toResponseFlag(absl::string_view flag) {
  // This `MapType` is introduce because CONSTRUCT_ON_FIRST_USE doesn't like template.
  using MapType = absl::flat_hash_map<std::string, ResponseFlag>;
  const auto& flag_map = []() {
    CONSTRUCT_ON_FIRST_USE(MapType, ResponseFlagUtils::getFlagMap());
  }();
  const auto& it = flag_map.find(flag);
  if (it != flag_map.end()) {
    return absl::make_optional<ResponseFlag>(it->second);
  }
  return absl::nullopt;
}

const std::string&
Utility::formatDownstreamAddressNoPort(const Network::Address::Instance& address) {
  if (address.type() == Network::Address::Type::Ip) {
    return address.ip()->addressAsString();
  } else {
    return address.asString();
  }
}

const std::string
Utility::formatDownstreamAddressJustPort(const Network::Address::Instance& address) {
  std::string port;
  if (address.type() == Network::Address::Type::Ip) {
    port = std::to_string(address.ip()->port());
  }
  return port;
}

} // namespace StreamInfo
} // namespace Envoy
