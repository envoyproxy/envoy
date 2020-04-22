#include "common/stream_info/utility.h"

#include <string>

namespace Envoy {
namespace StreamInfo {

const std::string ResponseFlagUtils::NONE = "-";
const std::string ResponseFlagUtils::DOWNSTREAM_CONNECTION_TERMINATION = "DC";
const std::string ResponseFlagUtils::FAILED_LOCAL_HEALTH_CHECK = "LH";
const std::string ResponseFlagUtils::NO_HEALTHY_UPSTREAM = "UH";
const std::string ResponseFlagUtils::UPSTREAM_REQUEST_TIMEOUT = "UT";
const std::string ResponseFlagUtils::LOCAL_RESET = "LR";
const std::string ResponseFlagUtils::UPSTREAM_REMOTE_RESET = "UR";
const std::string ResponseFlagUtils::UPSTREAM_CONNECTION_FAILURE = "UF";
const std::string ResponseFlagUtils::UPSTREAM_CONNECTION_TERMINATION = "UC";
const std::string ResponseFlagUtils::UPSTREAM_OVERFLOW = "UO";
const std::string ResponseFlagUtils::UPSTREAM_RETRY_LIMIT_EXCEEDED = "URX";
const std::string ResponseFlagUtils::NO_ROUTE_FOUND = "NR";
const std::string ResponseFlagUtils::DELAY_INJECTED = "DI";
const std::string ResponseFlagUtils::FAULT_INJECTED = "FI";
const std::string ResponseFlagUtils::RATE_LIMITED = "RL";
const std::string ResponseFlagUtils::UNAUTHORIZED_EXTERNAL_SERVICE = "UAEX";
const std::string ResponseFlagUtils::RATELIMIT_SERVICE_ERROR = "RLSE";
const std::string ResponseFlagUtils::STREAM_IDLE_TIMEOUT = "SI";
const std::string ResponseFlagUtils::INVALID_ENVOY_REQUEST_HEADERS = "IH";
const std::string ResponseFlagUtils::DOWNSTREAM_PROTOCOL_ERROR = "DPE";

void ResponseFlagUtils::appendString(std::string& result, const std::string& append) {
  if (result.empty()) {
    result = append;
  } else {
    result += "," + append;
  }
}

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  std::string result;

  static_assert(ResponseFlag::LastFlag == 0x40000, "A flag has been added. Fix this code.");

  if (stream_info.hasResponseFlag(ResponseFlag::FailedLocalHealthCheck)) {
    appendString(result, FAILED_LOCAL_HEALTH_CHECK);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::NoHealthyUpstream)) {
    appendString(result, NO_HEALTHY_UPSTREAM);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRequestTimeout)) {
    appendString(result, UPSTREAM_REQUEST_TIMEOUT);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::LocalReset)) {
    appendString(result, LOCAL_RESET);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRemoteReset)) {
    appendString(result, UPSTREAM_REMOTE_RESET);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamConnectionFailure)) {
    appendString(result, UPSTREAM_CONNECTION_FAILURE);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamConnectionTermination)) {
    appendString(result, UPSTREAM_CONNECTION_TERMINATION);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamOverflow)) {
    appendString(result, UPSTREAM_OVERFLOW);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::NoRouteFound)) {
    appendString(result, NO_ROUTE_FOUND);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::DelayInjected)) {
    appendString(result, DELAY_INJECTED);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::FaultInjected)) {
    appendString(result, FAULT_INJECTED);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::RateLimited)) {
    appendString(result, RATE_LIMITED);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UnauthorizedExternalService)) {
    appendString(result, UNAUTHORIZED_EXTERNAL_SERVICE);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::RateLimitServiceError)) {
    appendString(result, RATELIMIT_SERVICE_ERROR);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::DownstreamConnectionTermination)) {
    appendString(result, DOWNSTREAM_CONNECTION_TERMINATION);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::UpstreamRetryLimitExceeded)) {
    appendString(result, UPSTREAM_RETRY_LIMIT_EXCEEDED);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::StreamIdleTimeout)) {
    appendString(result, STREAM_IDLE_TIMEOUT);
  }

  if (stream_info.hasResponseFlag(ResponseFlag::InvalidEnvoyRequestHeaders)) {
    appendString(result, INVALID_ENVOY_REQUEST_HEADERS);
  }
  if (stream_info.hasResponseFlag(ResponseFlag::DownstreamProtocolError)) {
    appendString(result, DOWNSTREAM_PROTOCOL_ERROR);
  }

  return result.empty() ? NONE : result;
}

absl::optional<ResponseFlag> ResponseFlagUtils::toResponseFlag(const std::string& flag) {
  static const std::map<std::string, ResponseFlag> map = {
      {ResponseFlagUtils::FAILED_LOCAL_HEALTH_CHECK, ResponseFlag::FailedLocalHealthCheck},
      {ResponseFlagUtils::NO_HEALTHY_UPSTREAM, ResponseFlag::NoHealthyUpstream},
      {ResponseFlagUtils::UPSTREAM_REQUEST_TIMEOUT, ResponseFlag::UpstreamRequestTimeout},
      {ResponseFlagUtils::LOCAL_RESET, ResponseFlag::LocalReset},
      {ResponseFlagUtils::UPSTREAM_REMOTE_RESET, ResponseFlag::UpstreamRemoteReset},
      {ResponseFlagUtils::UPSTREAM_CONNECTION_FAILURE, ResponseFlag::UpstreamConnectionFailure},
      {ResponseFlagUtils::UPSTREAM_CONNECTION_TERMINATION,
       ResponseFlag::UpstreamConnectionTermination},
      {ResponseFlagUtils::UPSTREAM_OVERFLOW, ResponseFlag::UpstreamOverflow},
      {ResponseFlagUtils::NO_ROUTE_FOUND, ResponseFlag::NoRouteFound},
      {ResponseFlagUtils::DELAY_INJECTED, ResponseFlag::DelayInjected},
      {ResponseFlagUtils::FAULT_INJECTED, ResponseFlag::FaultInjected},
      {ResponseFlagUtils::RATE_LIMITED, ResponseFlag::RateLimited},
      {ResponseFlagUtils::UNAUTHORIZED_EXTERNAL_SERVICE, ResponseFlag::UnauthorizedExternalService},
      {ResponseFlagUtils::RATELIMIT_SERVICE_ERROR, ResponseFlag::RateLimitServiceError},
      {ResponseFlagUtils::DOWNSTREAM_CONNECTION_TERMINATION,
       ResponseFlag::DownstreamConnectionTermination},
      {ResponseFlagUtils::UPSTREAM_RETRY_LIMIT_EXCEEDED, ResponseFlag::UpstreamRetryLimitExceeded},
      {ResponseFlagUtils::STREAM_IDLE_TIMEOUT, ResponseFlag::StreamIdleTimeout},
      {ResponseFlagUtils::INVALID_ENVOY_REQUEST_HEADERS, ResponseFlag::InvalidEnvoyRequestHeaders},
      {ResponseFlagUtils::DOWNSTREAM_PROTOCOL_ERROR, ResponseFlag::DownstreamProtocolError},
  };
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
