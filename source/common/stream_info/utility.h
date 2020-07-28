#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Util class for ResponseFlags.
 */
class ResponseFlagUtils {
public:
  static const std::string toShortString(const StreamInfo& stream_info);
  static absl::optional<ResponseFlag> toResponseFlag(const std::string& response_flag);

private:
  ResponseFlagUtils();
  static void appendString(std::string& result, const std::string& append);

  const static std::string NONE;
  const static std::string DOWNSTREAM_CONNECTION_TERMINATION;
  const static std::string FAILED_LOCAL_HEALTH_CHECK;
  const static std::string NO_HEALTHY_UPSTREAM;
  const static std::string UPSTREAM_REQUEST_TIMEOUT;
  const static std::string LOCAL_RESET;
  const static std::string UPSTREAM_REMOTE_RESET;
  const static std::string UPSTREAM_CONNECTION_FAILURE;
  const static std::string UPSTREAM_CONNECTION_TERMINATION;
  const static std::string UPSTREAM_OVERFLOW;
  const static std::string UPSTREAM_RETRY_LIMIT_EXCEEDED;
  const static std::string NO_ROUTE_FOUND;
  const static std::string DELAY_INJECTED;
  const static std::string FAULT_INJECTED;
  const static std::string RATE_LIMITED;
  const static std::string UNAUTHORIZED_EXTERNAL_SERVICE;
  const static std::string RATELIMIT_SERVICE_ERROR;
  const static std::string STREAM_IDLE_TIMEOUT;
  const static std::string INVALID_ENVOY_REQUEST_HEADERS;
  const static std::string DOWNSTREAM_PROTOCOL_ERROR;
  const static std::string UPSTREAM_MAX_STREAM_DURATION_REACHED;
  const static std::string RESPONSE_FROM_CACHE_FILTER;
  const static std::string NO_FILTER_CONFIG_FOUND;
};

/**
 * Utility class for StreamInfo.
 */
class Utility {
public:
  /**
   * @param address supplies the downstream address.
   * @return a properly formatted address for logs, header expansion, etc.
   */
  static const std::string&
  formatDownstreamAddressNoPort(const Network::Address::Instance& address);

  /**
   * @param address supplies the downstream address.
   * @return a port, extracted from the provided downstream address for logs, header expansion, etc.
   */
  static const std::string
  formatDownstreamAddressJustPort(const Network::Address::Instance& address);
};

} // namespace StreamInfo
} // namespace Envoy
