#include "common/request_info/utility.h"

#include <string>

#include "envoy/request_info/request_info.h"

namespace Envoy {
namespace RequestInfo {

const std::string ResponseFlagUtils::NONE = "-";
const std::string ResponseFlagUtils::FAILED_LOCAL_HEALTH_CHECK = "LH";
const std::string ResponseFlagUtils::NO_HEALTHY_UPSTREAM = "UH";
const std::string ResponseFlagUtils::UPSTREAM_REQUEST_TIMEOUT = "UT";
const std::string ResponseFlagUtils::LOCAL_RESET = "LR";
const std::string ResponseFlagUtils::UPSTREAM_REMOTE_RESET = "UR";
const std::string ResponseFlagUtils::UPSTREAM_CONNECTION_FAILURE = "UF";
const std::string ResponseFlagUtils::UPSTREAM_CONNECTION_TERMINATION = "UC";
const std::string ResponseFlagUtils::UPSTREAM_OVERFLOW = "UO";
const std::string ResponseFlagUtils::NO_ROUTE_FOUND = "NR";
const std::string ResponseFlagUtils::DELAY_INJECTED = "DI";
const std::string ResponseFlagUtils::FAULT_INJECTED = "FI";
const std::string ResponseFlagUtils::RATE_LIMITED = "RL";
const std::string ResponseFlagUtils::UNAUTHORIZED = "UA";

void ResponseFlagUtils::appendString(std::string& result, const std::string& append) {
  if (result.empty()) {
    result = append;
  } else {
    result += "," + append;
  }
}

const std::string ResponseFlagUtils::toShortString(const RequestInfo& request_info) {
  std::string result;

  static_assert(ResponseFlag::LastFlag == 0x1000, "A flag has been added. Fix this code.");

  if (request_info.getResponseFlag(ResponseFlag::FailedLocalHealthCheck)) {
    appendString(result, FAILED_LOCAL_HEALTH_CHECK);
  }

  if (request_info.getResponseFlag(ResponseFlag::NoHealthyUpstream)) {
    appendString(result, NO_HEALTHY_UPSTREAM);
  }

  if (request_info.getResponseFlag(ResponseFlag::UpstreamRequestTimeout)) {
    appendString(result, UPSTREAM_REQUEST_TIMEOUT);
  }

  if (request_info.getResponseFlag(ResponseFlag::LocalReset)) {
    appendString(result, LOCAL_RESET);
  }

  if (request_info.getResponseFlag(ResponseFlag::UpstreamRemoteReset)) {
    appendString(result, UPSTREAM_REMOTE_RESET);
  }

  if (request_info.getResponseFlag(ResponseFlag::UpstreamConnectionFailure)) {
    appendString(result, UPSTREAM_CONNECTION_FAILURE);
  }

  if (request_info.getResponseFlag(ResponseFlag::UpstreamConnectionTermination)) {
    appendString(result, UPSTREAM_CONNECTION_TERMINATION);
  }

  if (request_info.getResponseFlag(ResponseFlag::UpstreamOverflow)) {
    appendString(result, UPSTREAM_OVERFLOW);
  }

  if (request_info.getResponseFlag(ResponseFlag::NoRouteFound)) {
    appendString(result, NO_ROUTE_FOUND);
  }

  if (request_info.getResponseFlag(ResponseFlag::DelayInjected)) {
    appendString(result, DELAY_INJECTED);
  }

  if (request_info.getResponseFlag(ResponseFlag::FaultInjected)) {
    appendString(result, FAULT_INJECTED);
  }

  if (request_info.getResponseFlag(ResponseFlag::RateLimited)) {
    appendString(result, RATE_LIMITED);
  }

  if (request_info.getResponseFlag(ResponseFlag::Unauthorized)) {
    appendString(result, UNAUTHORIZED);
  }

  return result.empty() ? NONE : result;
}

const std::string&
Utility::formatDownstreamAddressNoPort(const Network::Address::Instance& address) {
  if (address.type() == Network::Address::Type::Ip) {
    return address.ip()->addressAsString();
  } else {
    return address.asString();
  }
}

} // namespace RequestInfo
} // namespace Envoy
