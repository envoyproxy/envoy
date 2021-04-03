#include "common/stream_info/utility.h"

#include <string>

namespace Envoy {
namespace StreamInfo {

void ResponseFlagUtils::appendString(std::string& result, absl::string_view append) {
  if (result.empty()) {
    result = append;
  } else {
    result += ",";
    result += append;
  }
}

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  std::string result;

  for (const auto& [flag_string, flag] : ALL_RESPONSE_STRING_FLAGS) {
    if (stream_info.hasResponseFlag(flag)) {
      appendString(result, flag_string);
    }
  }

  return result.empty() ? std::string(NONE) : result;
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
  const auto& flag_map = []() { CONSTRUCT_ON_FIRST_USE(MapType, ResponseFlagUtils::getFlagMap()); }();
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
