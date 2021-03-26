#include "common/stream_info/utility.h"

#include <string>

namespace Envoy {
namespace StreamInfo {

// const std::array<std::pair<absl::string_view, ResponseFlag>>
//     ResponseFlagUtils::ALL_RESPONSE_STRING_FLAGS = {
//         {ResponseFlagUtils::FAILED_LOCAL_HEALTH_CHECK, ResponseFlag::FailedLocalHealthCheck},
//         {ResponseFlagUtils::NO_HEALTHY_UPSTREAM, ResponseFlag::NoHealthyUpstream},
//         {ResponseFlagUtils::UPSTREAM_REQUEST_TIMEOUT, ResponseFlag::UpstreamRequestTimeout},
//         {ResponseFlagUtils::LOCAL_RESET, ResponseFlag::LocalReset},
//         {ResponseFlagUtils::UPSTREAM_REMOTE_RESET, ResponseFlag::UpstreamRemoteReset},
//         {ResponseFlagUtils::UPSTREAM_CONNECTION_FAILURE,
//         ResponseFlag::UpstreamConnectionFailure},
//         {ResponseFlagUtils::UPSTREAM_CONNECTION_TERMINATION,
//          ResponseFlag::UpstreamConnectionTermination},
//         {ResponseFlagUtils::UPSTREAM_OVERFLOW, ResponseFlag::UpstreamOverflow},
//         {ResponseFlagUtils::NO_ROUTE_FOUND, ResponseFlag::NoRouteFound},
//         {ResponseFlagUtils::DELAY_INJECTED, ResponseFlag::DelayInjected},
//         {ResponseFlagUtils::FAULT_INJECTED, ResponseFlag::FaultInjected},
//         {ResponseFlagUtils::RATE_LIMITED, ResponseFlag::RateLimited},
//         {ResponseFlagUtils::UNAUTHORIZED_EXTERNAL_SERVICE,
//          ResponseFlag::UnauthorizedExternalService},
//         {ResponseFlagUtils::RATELIMIT_SERVICE_ERROR, ResponseFlag::RateLimitServiceError},
//         {ResponseFlagUtils::DOWNSTREAM_CONNECTION_TERMINATION,
//          ResponseFlag::DownstreamConnectionTermination},
//         {ResponseFlagUtils::UPSTREAM_RETRY_LIMIT_EXCEEDED,
//          ResponseFlag::UpstreamRetryLimitExceeded},
//         {ResponseFlagUtils::STREAM_IDLE_TIMEOUT, ResponseFlag::StreamIdleTimeout},
//         {ResponseFlagUtils::INVALID_ENVOY_REQUEST_HEADERS,
//          ResponseFlag::InvalidEnvoyRequestHeaders},
//         {ResponseFlagUtils::DOWNSTREAM_PROTOCOL_ERROR, ResponseFlag::DownstreamProtocolError},
//         {ResponseFlagUtils::UPSTREAM_MAX_STREAM_DURATION_REACHED,
//          ResponseFlag::UpstreamMaxStreamDurationReached},
//         {ResponseFlagUtils::RESPONSE_FROM_CACHE_FILTER, ResponseFlag::ResponseFromCacheFilter},
//         {ResponseFlagUtils::NO_FILTER_CONFIG_FOUND, ResponseFlag::NoFilterConfigFound},
//         {ResponseFlagUtils::DURATION_TIMEOUT, ResponseFlag::DurationTimeout},
//         {ResponseFlagUtils::UPSTREAM_PROTOCOL_ERROR, ResponseFlag::UpstreamProtocolError},
//         {ResponseFlagUtils::NO_CLUSTER_FOUND, ResponseFlag::NoClusterFound},
// };

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
  static const absl::flat_hash_map<std::string, ResponseFlag> map = ResponseFlagUtils::getFlagMap();
  const auto& it = map.find(flag);
  if (it != map.end()) {
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
