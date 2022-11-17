#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codes.h"
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
  constexpr static absl::string_view OVERLOAD_MANAGER = "OM";
  constexpr static absl::string_view DNS_FAIL = "DF";

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
      FlagStringAndEnum{OVERLOAD_MANAGER, ResponseFlag::OverloadManager},
  };

private:
  ResponseFlagUtils();
  static absl::flat_hash_map<std::string, ResponseFlag> getFlagMap();
};

class TimingUtility {
public:
  TimingUtility(const StreamInfo& info) : stream_info_(info) {}

  absl::optional<std::chrono::nanoseconds> firstUpstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastUpstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> firstDownstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> downstreamHandshakeComplete();

private:
  const StreamInfo& stream_info_;
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

// Static utils for creating, consuming, and producing strings from the
// Proxy-Status HTTP response header.
class ProxyStatusUtils {
public:
  // Returns a Proxy-Status proxy name string, configured according to |proxy_status_config|.
  // If |proxy_status_config| has not been set, defaults to |server_name|.
  static const std::string
  makeProxyName(absl::string_view node_id, absl::string_view server_name,
                const envoy::extensions::filters::network::http_connection_manager::v3::
                    HttpConnectionManager::ProxyStatusConfig* proxy_status_config);

  // Returns a Proxy-Status request header string, of the form:
  //
  //     <server_name>; error=<error_type>; details=<details>
  //
  // where:
  //   - node_id     is either the method argument, or the name of the proxy
  //                 in |node_id|,
  //   - error       is the error in |error|,
  //   - details     is |stream_info.responseCodeDetails()|, but the field is
  //                 present only if configured in |proxy_status_config|.
  static const std::string
  makeProxyStatusHeader(const StreamInfo& stream_info, ProxyStatusError error,
                        absl::string_view proxy_name,
                        const envoy::extensions::filters::network::http_connection_manager::v3::
                            HttpConnectionManager::ProxyStatusConfig& proxy_status_config);

  // Returns a view into the string representation of a given ProxyStatusError
  // enum.
  static const absl::string_view proxyStatusErrorToString(ProxyStatusError proxy_status);

  // Reads |stream_info.responseFlag| and returns an applicable ProxyStatusError, or nullopt
  // if no ProxyStatusError is applicable.
  static const absl::optional<ProxyStatusError> fromStreamInfo(const StreamInfo& stream_info);

  // Returns the recommended HTTP status code for a ProxyStatusError, or nullopt
  // if no HTTP status code is applicable.
  //
  // See
  // https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-05#section-2.1.1 :
  //
  // > Each Proxy Error Type has a Recommended HTTP Status Code. When
  // > generating a HTTP response containing "error", its HTTP status code
  // > SHOULD be set to the Recommended HTTP Status Code.
  static const absl::optional<Http::Code>
  recommendedHttpStatusCode(const ProxyStatusError proxy_status);

  constexpr static absl::string_view DNS_TIMEOUT = "dns_timeout";
  constexpr static absl::string_view DNS_ERROR = "dns_error";
  constexpr static absl::string_view DESTINATION_NOT_FOUND = "destination_not_found";
  constexpr static absl::string_view DESTINATION_UNAVAILABLE = "destination_unavailable";
  constexpr static absl::string_view DESTINATION_IP_PROHIBITED = "destination_ip_prohibited";
  constexpr static absl::string_view DESTINATION_IP_UNROUTABLE = "destination_ip_unroutable";
  constexpr static absl::string_view CONNECTION_REFUSED = "connection_refused";
  constexpr static absl::string_view CONNECTION_TERMINATED = "connection_terminated";
  constexpr static absl::string_view CONNECTION_TIMEOUT = "connection_timeout";
  constexpr static absl::string_view CONNECTION_READ_TIMEOUT = "connection_read_timeout";
  constexpr static absl::string_view CONNECTION_WRITE_TIMEOUT = "connection_write_timeout";
  constexpr static absl::string_view CONNECTION_LIMIT_REACHED = "connection_limit_reached";
  constexpr static absl::string_view TLS_PROTOCOL_ERROR = "tls_protocol_error";
  constexpr static absl::string_view TLS_CERTIFICATE_ERORR = "tls_certificate_error";
  constexpr static absl::string_view TLS_ALERT_RECEIVED = "tls_alert_received";
  constexpr static absl::string_view HTTP_REQUEST_ERROR = "http_request_error";
  constexpr static absl::string_view HTTP_REQUEST_DENIED = "http_request_denied";
  constexpr static absl::string_view HTTP_RESPONSE_INCOMPLETE = "http_response_incomplete";
  constexpr static absl::string_view HTTP_RESPONSE_HEADER_SECTION_SIZE =
      "http_response_header_section_size";
  constexpr static absl::string_view HTTP_RESPONSE_HEADER_SIZE = "http_response_header_size";
  constexpr static absl::string_view HTTP_RESPONSE_BODY_SIZE = "http_response_body_size";
  constexpr static absl::string_view HTTP_RESPONSE_TRAILER_SECTION_SIZE =
      "http_response_trailer_section_size";
  constexpr static absl::string_view HTTP_RESPONSE_TRAILER_SIZE = "http_response_trailer_size";
  constexpr static absl::string_view HTTP_RESPONSE_TRANSFER_CODING =
      "http_response_transfer_coding";
  constexpr static absl::string_view HTTP_RESPONSE_CONTENT_CODING = "http_response_content_coding";
  constexpr static absl::string_view HTTP_RESPONSE_TIMEOUT = "http_response_timeout";
  constexpr static absl::string_view HTTP_UPGRADE_FAILED = "http_upgrade_failed";
  constexpr static absl::string_view HTTP_PROTOCOL_ERROR = "http_protocol_error";
  constexpr static absl::string_view PROXY_INTERNAL_RESPONSE = "proxy_internal_response";
  constexpr static absl::string_view PROXY_INTERNAL_ERROR = "proxy_internal_error";
  constexpr static absl::string_view PROXY_CONFIGURATION_ERROR = "proxy_configuration_error";
  constexpr static absl::string_view PROXY_LOOP_DETECTED = "proxy_loop_detected";
};

} // namespace StreamInfo
} // namespace Envoy
