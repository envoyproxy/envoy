#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codes.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace StreamInfo {

class CustomResponseFlag {
public:
  CustomResponseFlag(absl::string_view flag, absl::string_view flag_long);
  ResponseFlag flag() const { return flag_; }

private:
  ResponseFlag flag_;
};

// Register a custom response flag by specifying the flag and the long name of the flag.
// This macro should only be used in source files to register a flag.
#define REGISTER_CUSTOM_RESPONSE_FLAG(flag_short_string, flag_long_string)                         \
  static CustomResponseFlag /* NOLINT(fuchsia-statically-constructed-objects) */                   \
      registered_##flag_short_string{#flag_short_string, #flag_long_string};

// Get the registered flag value. This macro should only be used when calling the
// 'setResponseFlag' method in the StreamInfo class.
// NOTE: Never use this macro to initialize another static variable.
// Basically, this macro should only be used in the same source file where the flag is
// registered.
// If you want to use one flag in multiple files, you can declare a static function in
// the header file and define it in the source file to return the flag value. Here is an
// example (NOTE: this function should obey the same rule as the CUSTOM_RESPONSE_FLAG
// macro and cannot be used to initialize another static variable):
//
// // header.h
// ResponseFlag getRegisteredFlag();
// // source.cc
// REGISTER_CUSTOM_RESPONSE_FLAG(CF, CustomFlag);
// ResponseFlag getRegisteredFlag() { return CUSTOM_RESPONSE_FLAG(CF); }
#define CUSTOM_RESPONSE_FLAG(flag_short_string) registered_##flag_short_string.flag()

/**
 * Util class for ResponseFlags.
 */
class ResponseFlagUtils {
public:
  static const std::string toString(const StreamInfo& stream_info);
  static const std::string toShortString(const StreamInfo& stream_info);
  static absl::optional<ResponseFlag> toResponseFlag(absl::string_view response_flag);

  struct FlagStrings {
    absl::string_view short_string_;
    absl::string_view long_string_; // PascalCase string
    ResponseFlag flag_;
  };

  struct FlagLongString {
    ResponseFlag flag_;
    std::string long_string_; // PascalCase string
  };

  using ResponseFlagsVecType = std::vector<FlagStrings>;
  // Node hash map is used to avoid the key/value pair pointer change of the string_view when the
  // map is resized. And the performance is not a concern here because the map is only used when
  // loading the config.
  using ResponseFlagsMapType = absl::node_hash_map<std::string, FlagLongString>;
  static const ResponseFlagsVecType& responseFlagsVec();
  static const ResponseFlagsMapType& responseFlagsMap();

  // When adding a new flag, it's required to update the access log docs and the string
  // mapping below - ``CORE_RESPONSE_FLAGS``.
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
  constexpr static absl::string_view DROP_OVERLOAD = "DO";
  constexpr static absl::string_view DOWNSTREAM_REMOTE_RESET = "DR";

  constexpr static absl::string_view DOWNSTREAM_CONNECTION_TERMINATION_LONG =
      "DownstreamConnectionTermination";
  constexpr static absl::string_view FAILED_LOCAL_HEALTH_CHECK_LONG = "FailedLocalHealthCheck";
  constexpr static absl::string_view NO_HEALTHY_UPSTREAM_LONG = "NoHealthyUpstream";
  constexpr static absl::string_view UPSTREAM_REQUEST_TIMEOUT_LONG = "UpstreamRequestTimeout";
  constexpr static absl::string_view LOCAL_RESET_LONG = "LocalReset";
  constexpr static absl::string_view UPSTREAM_REMOTE_RESET_LONG = "UpstreamRemoteReset";
  constexpr static absl::string_view UPSTREAM_CONNECTION_FAILURE_LONG = "UpstreamConnectionFailure";
  constexpr static absl::string_view UPSTREAM_CONNECTION_TERMINATION_LONG =
      "UpstreamConnectionTermination";
  constexpr static absl::string_view UPSTREAM_OVERFLOW_LONG = "UpstreamOverflow";
  constexpr static absl::string_view UPSTREAM_RETRY_LIMIT_EXCEEDED_LONG =
      "UpstreamRetryLimitExceeded";
  constexpr static absl::string_view NO_ROUTE_FOUND_LONG = "NoRouteFound";
  constexpr static absl::string_view DELAY_INJECTED_LONG = "DelayInjected";
  constexpr static absl::string_view FAULT_INJECTED_LONG = "FaultInjected";
  constexpr static absl::string_view RATE_LIMITED_LONG = "RateLimited";
  constexpr static absl::string_view UNAUTHORIZED_EXTERNAL_SERVICE_LONG =
      "UnauthorizedExternalService";
  constexpr static absl::string_view RATELIMIT_SERVICE_ERROR_LONG = "RateLimitServiceError";
  constexpr static absl::string_view STREAM_IDLE_TIMEOUT_LONG = "StreamIdleTimeout";
  constexpr static absl::string_view INVALID_ENVOY_REQUEST_HEADERS_LONG =
      "InvalidEnvoyRequestHeaders";
  constexpr static absl::string_view DOWNSTREAM_PROTOCOL_ERROR_LONG = "DownstreamProtocolError";
  constexpr static absl::string_view UPSTREAM_MAX_STREAM_DURATION_REACHED_LONG =
      "UpstreamMaxStreamDurationReached";
  constexpr static absl::string_view RESPONSE_FROM_CACHE_FILTER_LONG = "ResponseFromCacheFilter";
  constexpr static absl::string_view NO_FILTER_CONFIG_FOUND_LONG = "NoFilterConfigFound";
  constexpr static absl::string_view DURATION_TIMEOUT_LONG = "DurationTimeout";
  constexpr static absl::string_view UPSTREAM_PROTOCOL_ERROR_LONG = "UpstreamProtocolError";
  constexpr static absl::string_view NO_CLUSTER_FOUND_LONG = "NoClusterFound";
  constexpr static absl::string_view OVERLOAD_MANAGER_LONG = "OverloadManagerTerminated";
  constexpr static absl::string_view DNS_FAIL_LONG = "DnsResolutionFailed";
  constexpr static absl::string_view DROP_OVERLOAD_LONG = "DropOverload";
  constexpr static absl::string_view DOWNSTREAM_REMOTE_RESET_LONG = "DownstreamRemoteReset";

  static constexpr std::array CORE_RESPONSE_FLAGS{
      FlagStrings{FAILED_LOCAL_HEALTH_CHECK, FAILED_LOCAL_HEALTH_CHECK_LONG,
                  CoreResponseFlag::FailedLocalHealthCheck},
      FlagStrings{NO_HEALTHY_UPSTREAM, NO_HEALTHY_UPSTREAM_LONG,
                  CoreResponseFlag::NoHealthyUpstream},
      FlagStrings{UPSTREAM_REQUEST_TIMEOUT, UPSTREAM_REQUEST_TIMEOUT_LONG,
                  CoreResponseFlag::UpstreamRequestTimeout},
      FlagStrings{LOCAL_RESET, LOCAL_RESET_LONG, CoreResponseFlag::LocalReset},
      FlagStrings{UPSTREAM_REMOTE_RESET, UPSTREAM_REMOTE_RESET_LONG,
                  CoreResponseFlag::UpstreamRemoteReset},
      FlagStrings{UPSTREAM_CONNECTION_FAILURE, UPSTREAM_CONNECTION_FAILURE_LONG,
                  CoreResponseFlag::UpstreamConnectionFailure},
      FlagStrings{UPSTREAM_CONNECTION_TERMINATION, UPSTREAM_CONNECTION_TERMINATION_LONG,
                  CoreResponseFlag::UpstreamConnectionTermination},
      FlagStrings{UPSTREAM_OVERFLOW, UPSTREAM_OVERFLOW_LONG, CoreResponseFlag::UpstreamOverflow},
      FlagStrings{NO_ROUTE_FOUND, NO_ROUTE_FOUND_LONG, CoreResponseFlag::NoRouteFound},
      FlagStrings{DELAY_INJECTED, DELAY_INJECTED_LONG, CoreResponseFlag::DelayInjected},
      FlagStrings{FAULT_INJECTED, FAULT_INJECTED_LONG, CoreResponseFlag::FaultInjected},
      FlagStrings{RATE_LIMITED, RATE_LIMITED_LONG, CoreResponseFlag::RateLimited},
      FlagStrings{UNAUTHORIZED_EXTERNAL_SERVICE, UNAUTHORIZED_EXTERNAL_SERVICE_LONG,
                  CoreResponseFlag::UnauthorizedExternalService},
      FlagStrings{RATELIMIT_SERVICE_ERROR, RATELIMIT_SERVICE_ERROR_LONG,
                  CoreResponseFlag::RateLimitServiceError},
      FlagStrings{DOWNSTREAM_CONNECTION_TERMINATION, DOWNSTREAM_CONNECTION_TERMINATION_LONG,
                  CoreResponseFlag::DownstreamConnectionTermination},
      FlagStrings{UPSTREAM_RETRY_LIMIT_EXCEEDED, UPSTREAM_RETRY_LIMIT_EXCEEDED_LONG,
                  CoreResponseFlag::UpstreamRetryLimitExceeded},
      FlagStrings{STREAM_IDLE_TIMEOUT, STREAM_IDLE_TIMEOUT_LONG,
                  CoreResponseFlag::StreamIdleTimeout},
      FlagStrings{INVALID_ENVOY_REQUEST_HEADERS, INVALID_ENVOY_REQUEST_HEADERS_LONG,
                  CoreResponseFlag::InvalidEnvoyRequestHeaders},
      FlagStrings{DOWNSTREAM_PROTOCOL_ERROR, DOWNSTREAM_PROTOCOL_ERROR_LONG,
                  CoreResponseFlag::DownstreamProtocolError},
      FlagStrings{UPSTREAM_MAX_STREAM_DURATION_REACHED, UPSTREAM_MAX_STREAM_DURATION_REACHED_LONG,
                  CoreResponseFlag::UpstreamMaxStreamDurationReached},
      FlagStrings{RESPONSE_FROM_CACHE_FILTER, RESPONSE_FROM_CACHE_FILTER_LONG,
                  CoreResponseFlag::ResponseFromCacheFilter},
      FlagStrings{NO_FILTER_CONFIG_FOUND, NO_FILTER_CONFIG_FOUND_LONG,
                  CoreResponseFlag::NoFilterConfigFound},
      FlagStrings{DURATION_TIMEOUT, DURATION_TIMEOUT_LONG, CoreResponseFlag::DurationTimeout},
      FlagStrings{UPSTREAM_PROTOCOL_ERROR, UPSTREAM_PROTOCOL_ERROR_LONG,
                  CoreResponseFlag::UpstreamProtocolError},
      FlagStrings{NO_CLUSTER_FOUND, NO_CLUSTER_FOUND_LONG, CoreResponseFlag::NoClusterFound},
      FlagStrings{OVERLOAD_MANAGER, OVERLOAD_MANAGER_LONG, CoreResponseFlag::OverloadManager},
      FlagStrings{DNS_FAIL, DNS_FAIL_LONG, CoreResponseFlag::DnsResolutionFailed},
      FlagStrings{DROP_OVERLOAD, DROP_OVERLOAD_LONG, CoreResponseFlag::DropOverLoad},
      FlagStrings{DOWNSTREAM_REMOTE_RESET, DOWNSTREAM_REMOTE_RESET_LONG,
                  CoreResponseFlag::DownstreamRemoteReset},
  };

private:
  friend class CustomResponseFlag;

  static const std::string toString(const StreamInfo& stream_info, bool use_long_name);

  /**
   * Register a custom response flag.
   * @param flag supplies the flag to register. It should be an all upper case string with only
   * multiple characters.
   * @param flag_long supplies the long name of the flag to register. It should be PascalCase
   * string.
   * @return uint16_t the flag value.
   */
  static ResponseFlag registerCustomFlag(absl::string_view flag, absl::string_view flag_long);
  static ResponseFlagsMapType& mutableResponseFlagsMap();
};

class TimingUtility {
public:
  TimingUtility(const StreamInfo& info) : stream_info_(info) {}

  absl::optional<std::chrono::nanoseconds> firstUpstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastUpstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> upstreamHandshakeComplete();
  absl::optional<std::chrono::nanoseconds> firstDownstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent();
  absl::optional<std::chrono::nanoseconds> lastDownstreamHeaderRxByteReceived();
  absl::optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived();
  absl::optional<std::chrono::nanoseconds> downstreamHandshakeComplete();
  absl::optional<std::chrono::nanoseconds> lastDownstreamAckReceived();

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

  /**
   * @param address supplies the downstream address.
   * @return a port, extracted from the provided downstream address for logs, header expansion, etc.
   */
  static absl::optional<uint32_t>
  extractDownstreamAddressJustPort(const Network::Address::Instance& address);
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
  constexpr static absl::string_view TLS_CERTIFICATE_ERROR = "tls_certificate_error";
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
