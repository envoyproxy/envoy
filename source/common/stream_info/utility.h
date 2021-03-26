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
      std::pair<const absl::string_view, ResponseFlag>{FAILED_LOCAL_HEALTH_CHECK,
                                                       ResponseFlag::FailedLocalHealthCheck},
      std::pair<const absl::string_view, ResponseFlag>{NO_HEALTHY_UPSTREAM,
                                                       ResponseFlag::NoHealthyUpstream},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_REQUEST_TIMEOUT,
                                                       ResponseFlag::UpstreamRequestTimeout},
      std::pair<const absl::string_view, ResponseFlag>{LOCAL_RESET, ResponseFlag::LocalReset},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_REMOTE_RESET,
                                                       ResponseFlag::UpstreamRemoteReset},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_CONNECTION_FAILURE,
                                                       ResponseFlag::UpstreamConnectionFailure},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_CONNECTION_TERMINATION,
                                                       ResponseFlag::UpstreamConnectionTermination},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_OVERFLOW,
                                                       ResponseFlag::UpstreamOverflow},
      std::pair<const absl::string_view, ResponseFlag>{NO_ROUTE_FOUND, ResponseFlag::NoRouteFound},
      std::pair<const absl::string_view, ResponseFlag>{DELAY_INJECTED, ResponseFlag::DelayInjected},
      std::pair<const absl::string_view, ResponseFlag>{FAULT_INJECTED, ResponseFlag::FaultInjected},
      std::pair<const absl::string_view, ResponseFlag>{RATE_LIMITED, ResponseFlag::RateLimited},
      std::pair<const absl::string_view, ResponseFlag>{UNAUTHORIZED_EXTERNAL_SERVICE,
                                                       ResponseFlag::UnauthorizedExternalService},
      std::pair<const absl::string_view, ResponseFlag>{RATELIMIT_SERVICE_ERROR,
                                                       ResponseFlag::RateLimitServiceError},
      std::pair<const absl::string_view, ResponseFlag>{
          DOWNSTREAM_CONNECTION_TERMINATION, ResponseFlag::DownstreamConnectionTermination},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_RETRY_LIMIT_EXCEEDED,
                                                       ResponseFlag::UpstreamRetryLimitExceeded},
      std::pair<const absl::string_view, ResponseFlag>{STREAM_IDLE_TIMEOUT,
                                                       ResponseFlag::StreamIdleTimeout},
      std::pair<const absl::string_view, ResponseFlag>{INVALID_ENVOY_REQUEST_HEADERS,
                                                       ResponseFlag::InvalidEnvoyRequestHeaders},
      std::pair<const absl::string_view, ResponseFlag>{DOWNSTREAM_PROTOCOL_ERROR,
                                                       ResponseFlag::DownstreamProtocolError},
      std::pair<const absl::string_view, ResponseFlag>{
          UPSTREAM_MAX_STREAM_DURATION_REACHED, ResponseFlag::UpstreamMaxStreamDurationReached},
      std::pair<const absl::string_view, ResponseFlag>{RESPONSE_FROM_CACHE_FILTER,
                                                       ResponseFlag::ResponseFromCacheFilter},
      std::pair<const absl::string_view, ResponseFlag>{NO_FILTER_CONFIG_FOUND,
                                                       ResponseFlag::NoFilterConfigFound},
      std::pair<const absl::string_view, ResponseFlag>{DURATION_TIMEOUT,
                                                       ResponseFlag::DurationTimeout},
      std::pair<const absl::string_view, ResponseFlag>{UPSTREAM_PROTOCOL_ERROR,
                                                       ResponseFlag::UpstreamProtocolError},
      std::pair<const absl::string_view, ResponseFlag>{NO_CLUSTER_FOUND,
                                                       ResponseFlag::NoClusterFound},
  };

private:
  ResponseFlagUtils();
  static absl::flat_hash_map<std::string, ResponseFlag> getFlagMap();

  static void appendString(std::string& result, absl::string_view append);
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
