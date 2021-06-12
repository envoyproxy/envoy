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
  static absl::optional<ResponseFlag> toResponseFlag(absl::string_view response_flag);

  using FlagStringAndEnum = std::pair<const absl::string_view, ResponseFlag>;

  constexpr static absl::string_view NONE = "-";
  constexpr static absl::string_view DOWNSTREAM_CONNECTION_TERMINATION = "DC";
  constexpr static absl::string_view FAILED_LOCAL_HEALTH_CHECK = "LH";
  constexpr static absl::string_view NO_HEALTHY_UPSTREAM = "UH";
  constexpr static absl::string_view UPSTREAM_REQUEST_TIMEOUT = "UT";
  constexpr static absl::string_view LOCAL_RESET = "LR";
  constexpr static absl::string_view UPSTREAM_REMOTE_RESET = "UR";
  constexpr static absl::string_view UPSTREAM_CONNECTION_FAILURE = "UF";
  constexpr static absl::string_view UPSTREAM_CONNECTION_TERMINATION = "UC";
  constexpr static absl::string_view UPSTREAM_OVERFLOW = "UO";
  constexpr static absl::string_view UPSTREAM_RETRY_LIMIT_EXCEEDED = "URX";
  constexpr static absl::string_view NO_ROUTE_FOUND = "NR";
  constexpr static absl::string_view DELAY_INJECTED = "DI";
  constexpr static absl::string_view FAULT_INJECTED = "FI";
  constexpr static absl::string_view RATE_LIMITED = "RL";
  constexpr static absl::string_view UNAUTHORIZED_EXTERNAL_SERVICE = "UAEX";
  constexpr static absl::string_view RATELIMIT_SERVICE_ERROR = "RLSE";
  constexpr static absl::string_view STREAM_IDLE_TIMEOUT = "SI";
  constexpr static absl::string_view INVALID_ENVOY_REQUEST_HEADERS = "IH";
  constexpr static absl::string_view DOWNSTREAM_PROTOCOL_ERROR = "DPE";
  constexpr static absl::string_view UPSTREAM_MAX_STREAM_DURATION_REACHED = "UMSDR";
  constexpr static absl::string_view RESPONSE_FROM_CACHE_FILTER = "RFCF";
  constexpr static absl::string_view NO_FILTER_CONFIG_FOUND = "NFCF";
  constexpr static absl::string_view DURATION_TIMEOUT = "DT";
  constexpr static absl::string_view UPSTREAM_PROTOCOL_ERROR = "UPE";
  constexpr static absl::string_view NO_CLUSTER_FOUND = "NC";

  static constexpr std::array ALL_RESPONSE_STRING_FLAGS{
      FlagStringAndEnum{FAILED_LOCAL_HEALTH_CHECK, ResponseFlag::FailedLocalHealthCheck},
      FlagStringAndEnum{NO_HEALTHY_UPSTREAM, ResponseFlag::NoHealthyUpstream},
      FlagStringAndEnum{UPSTREAM_REQUEST_TIMEOUT, ResponseFlag::UpstreamRequestTimeout},
      FlagStringAndEnum{LOCAL_RESET, ResponseFlag::LocalReset},
      FlagStringAndEnum{UPSTREAM_REMOTE_RESET, ResponseFlag::UpstreamRemoteReset},
      FlagStringAndEnum{UPSTREAM_CONNECTION_FAILURE, ResponseFlag::UpstreamConnectionFailure},
      FlagStringAndEnum{UPSTREAM_CONNECTION_TERMINATION,
                        ResponseFlag::UpstreamConnectionTermination},
      FlagStringAndEnum{UPSTREAM_OVERFLOW, ResponseFlag::UpstreamOverflow},
      FlagStringAndEnum{NO_ROUTE_FOUND, ResponseFlag::NoRouteFound},
      FlagStringAndEnum{DELAY_INJECTED, ResponseFlag::DelayInjected},
      FlagStringAndEnum{FAULT_INJECTED, ResponseFlag::FaultInjected},
      FlagStringAndEnum{RATE_LIMITED, ResponseFlag::RateLimited},
      FlagStringAndEnum{UNAUTHORIZED_EXTERNAL_SERVICE, ResponseFlag::UnauthorizedExternalService},
      FlagStringAndEnum{RATELIMIT_SERVICE_ERROR, ResponseFlag::RateLimitServiceError},
      FlagStringAndEnum{DOWNSTREAM_CONNECTION_TERMINATION,
                        ResponseFlag::DownstreamConnectionTermination},
      FlagStringAndEnum{UPSTREAM_RETRY_LIMIT_EXCEEDED, ResponseFlag::UpstreamRetryLimitExceeded},
      FlagStringAndEnum{STREAM_IDLE_TIMEOUT, ResponseFlag::StreamIdleTimeout},
      FlagStringAndEnum{INVALID_ENVOY_REQUEST_HEADERS, ResponseFlag::InvalidEnvoyRequestHeaders},
      FlagStringAndEnum{DOWNSTREAM_PROTOCOL_ERROR, ResponseFlag::DownstreamProtocolError},
      FlagStringAndEnum{UPSTREAM_MAX_STREAM_DURATION_REACHED,
                        ResponseFlag::UpstreamMaxStreamDurationReached},
      FlagStringAndEnum{RESPONSE_FROM_CACHE_FILTER, ResponseFlag::ResponseFromCacheFilter},
      FlagStringAndEnum{NO_FILTER_CONFIG_FOUND, ResponseFlag::NoFilterConfigFound},
      FlagStringAndEnum{DURATION_TIMEOUT, ResponseFlag::DurationTimeout},
      FlagStringAndEnum{UPSTREAM_PROTOCOL_ERROR, ResponseFlag::UpstreamProtocolError},
      FlagStringAndEnum{NO_CLUSTER_FOUND, ResponseFlag::NoClusterFound},
  };

private:
  ResponseFlagUtils();
  static absl::flat_hash_map<std::string, ResponseFlag> getFlagMap();
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
