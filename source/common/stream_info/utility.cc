#include "source/common/stream_info/utility.h"

#include <string>

namespace Envoy {
namespace StreamInfo {

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  // We don't expect more than 4 flags are set. Relax to 16 since the vector is allocated on stack
  // anyway.
  absl::InlinedVector<absl::string_view, 16> flag_strings;
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
  static_assert(ResponseFlag::LastFlag == 0x2000000,
                "A flag has been added. Add the new flag to ALL_RESPONSE_STRING_FLAGS.");
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

OptRef<const UpstreamTiming> getUpstreamTiming(const StreamInfo& stream_info) {
  OptRef<const UpstreamInfo> info = stream_info.upstreamInfo();
  if (!info.has_value()) {
    return {};
  }
  return info.value().get().upstreamTiming();
}

absl::optional<std::chrono::nanoseconds> duration(const absl::optional<MonotonicTime>& time,
                                                  const StreamInfo& stream_info) {
  if (!time.has_value()) {
    return absl::nullopt;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() -
                                                              stream_info.startTimeMonotonic());
}

absl::optional<std::chrono::nanoseconds> TimingUtility::firstUpstreamTxByteSent() {
  OptRef<const UpstreamTiming> timing = getUpstreamTiming(stream_info_);
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().first_upstream_tx_byte_sent_, stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastUpstreamTxByteSent() {
  OptRef<const UpstreamTiming> timing = getUpstreamTiming(stream_info_);
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().last_upstream_tx_byte_sent_, stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::firstUpstreamRxByteReceived() {
  OptRef<const UpstreamTiming> timing = getUpstreamTiming(stream_info_);
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().first_upstream_rx_byte_received_, stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastUpstreamRxByteReceived() {
  OptRef<const UpstreamTiming> timing = getUpstreamTiming(stream_info_);
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().last_upstream_rx_byte_received_, stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::firstDownstreamTxByteSent() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().firstDownstreamTxByteSent(), stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastDownstreamTxByteSent() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().lastDownstreamTxByteSent(), stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastDownstreamRxByteReceived() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().lastDownstreamRxByteReceived(), stream_info_);
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
