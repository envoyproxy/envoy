#include "source/common/stream_info/utility.h"

#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/http/default_server_string.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace StreamInfo {

namespace {

// This flag is used to ensure that the responseFlagsVec() contains all the flags and no
// any new custom flags are registered after the responseFlagsVec() is initialized.
// NOTE: we expect all registrations of custom flags to happen during static initialization
// before the first use of responseFlagsVec(). So no thread safety is needed here.
bool& responseFlagsVecInitialized() { MUTABLE_CONSTRUCT_ON_FIRST_USE(bool, false); }

} // namespace

const std::string ResponseFlagUtils::toString(const StreamInfo& stream_info) {
  return toString(stream_info, true);
}

const std::string ResponseFlagUtils::toShortString(const StreamInfo& stream_info) {
  return toString(stream_info, false);
}

const std::string ResponseFlagUtils::toString(const StreamInfo& stream_info, bool use_long_name) {
  // We don't expect more than 4 flags are set. Relax to 16 since the vector is allocated on stack
  // anyway.
  absl::InlinedVector<absl::string_view, 16> flag_strings_vec;

  const auto& all_flag_strings = responseFlagsVec();
  for (const auto flag : stream_info.responseFlags()) {
    ASSERT(flag.value() < all_flag_strings.size(), "Flag value out of range");

    const auto flag_strings = all_flag_strings[flag.value()];
    flag_strings_vec.push_back(use_long_name ? flag_strings.long_string_
                                             : flag_strings.short_string_);
  }
  if (flag_strings_vec.empty()) {
    return std::string(NONE);
  }
  return absl::StrJoin(flag_strings_vec, ",");
}

ResponseFlagUtils::ResponseFlagsMapType& ResponseFlagUtils::mutableResponseFlagsMap() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(ResponseFlagsMapType, []() {
    ResponseFlagsMapType map;
    // Initialize the map with the all core flags first to ensure no custom flags
    // conflict with them.
    RELEASE_ASSERT(CORE_RESPONSE_FLAGS.size() == CoreResponseFlag::LastFlag + 1,
                   "Not all inlined flags are contained by CORE_RESPONSE_FLAGS.");

    map.reserve(CORE_RESPONSE_FLAGS.size());
    for (const auto& flag : CORE_RESPONSE_FLAGS) {
      map.emplace(flag.short_string_, FlagLongString{flag.flag_, std::string(flag.long_string_)});
    }
    RELEASE_ASSERT(map.size() == CORE_RESPONSE_FLAGS.size(),
                   "Duplicate flags in CORE_RESPONSE_FLAGS");
    return map;
  }());
}

ResponseFlag ResponseFlagUtils::registerCustomFlag(absl::string_view custom_flag,
                                                   absl::string_view custom_flag_long) {
  auto& mutable_flags = mutableResponseFlagsMap();

  RELEASE_ASSERT(!responseFlagsVecInitialized(),
                 "Cannot register custom flags after initialization");

  RELEASE_ASSERT(!mutable_flags.contains(custom_flag),
                 fmt::format("Flag: {}/{} already registered", custom_flag, custom_flag_long));

  const uint16_t next_flag = mutable_flags.size();

  mutable_flags.emplace(custom_flag, FlagLongString{next_flag, std::string(custom_flag_long)});

  return next_flag;
}

const ResponseFlagUtils::ResponseFlagsVecType& ResponseFlagUtils::responseFlagsVec() {
  CONSTRUCT_ON_FIRST_USE(ResponseFlagsVecType, []() {
    static_assert(CoreResponseFlag::LastFlag == 28,
                  "A flag has been added. Add the new flag to CORE_RESPONSE_FLAGS.");

    responseFlagsVecInitialized() = true;

    ResponseFlagsVecType res;

    uint16_t max_flag = CoreResponseFlag::LastFlag;
    for (const auto& flag : responseFlagsMap()) {
      if (flag.second.flag_.value() > max_flag) {
        max_flag = flag.second.flag_.value();
      }
    }

    res.resize(max_flag + 1);

    for (const auto& flag : responseFlagsMap()) {
      res[flag.second.flag_.value()] = {absl::string_view(flag.first),
                                        absl::string_view(flag.second.long_string_),
                                        flag.second.flag_};
    }

    return res;
  }());
}

const ResponseFlagUtils::ResponseFlagsMapType& ResponseFlagUtils::responseFlagsMap() {
  return mutableResponseFlagsMap();
}

absl::optional<ResponseFlag> ResponseFlagUtils::toResponseFlag(absl::string_view flag) {
  const auto iter = responseFlagsMap().find(flag);
  if (iter != responseFlagsMap().end()) {
    return iter->second.flag_;
  }
  return absl::nullopt;
}

CustomResponseFlag::CustomResponseFlag(absl::string_view flag, absl::string_view flag_long)
    : flag_(ResponseFlagUtils::registerCustomFlag(flag, flag_long)) {}

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

absl::optional<std::chrono::nanoseconds> TimingUtility::upstreamHandshakeComplete() {
  OptRef<const UpstreamTiming> timing = getUpstreamTiming(stream_info_);
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().upstreamHandshakeComplete(), stream_info_);
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

absl::optional<std::chrono::nanoseconds> TimingUtility::lastDownstreamHeaderRxByteReceived() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().lastDownstreamHeaderRxByteReceived(), stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastDownstreamRxByteReceived() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().lastDownstreamRxByteReceived(), stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::downstreamHandshakeComplete() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().downstreamHandshakeComplete(), stream_info_);
}

absl::optional<std::chrono::nanoseconds> TimingUtility::lastDownstreamAckReceived() {
  OptRef<const DownstreamTiming> timing = stream_info_.downstreamTiming();
  if (!timing) {
    return absl::nullopt;
  }
  return duration(timing.value().get().lastDownstreamAckReceived(), stream_info_);
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

absl::optional<uint32_t>
Utility::extractDownstreamAddressJustPort(const Network::Address::Instance& address) {
  if (address.type() == Network::Address::Type::Ip) {
    return address.ip()->port();
  }
  return {};
}

const absl::optional<Http::Code>
ProxyStatusUtils::recommendedHttpStatusCode(const ProxyStatusError proxy_status) {
  // This switch statement was derived from the mapping from proxy error type to
  // recommended HTTP status code in
  // https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-05#section-2.3 and below.
  //
  // TODO(ambuc): Replace this with the non-draft URL when finalized.
  switch (proxy_status) {
  case ProxyStatusError::DnsTimeout:
  case ProxyStatusError::ConnectionTimeout:
  case ProxyStatusError::ConnectionReadTimeout:
  case ProxyStatusError::ConnectionWriteTimeout:
  case ProxyStatusError::HttpResponseTimeout:
    return Http::Code::GatewayTimeout; // 504
  case ProxyStatusError::DnsError:
  case ProxyStatusError::DestinationIpProhibited:
  case ProxyStatusError::DestinationIpUnroutable:
  case ProxyStatusError::ConnectionRefused:
  case ProxyStatusError::ConnectionTerminated:
  case ProxyStatusError::TlsProtocolError:
  case ProxyStatusError::TlsCertificateError:
  case ProxyStatusError::TlsAlertReceived:
  case ProxyStatusError::HttpResponseIncomplete:
  case ProxyStatusError::HttpResponseHeaderSectionSize:
  case ProxyStatusError::HttpResponseHeaderSize:
  case ProxyStatusError::HttpResponseBodySize:
  case ProxyStatusError::HttpResponseTrailerSectionSize:
  case ProxyStatusError::HttpResponseTrailerSize:
  case ProxyStatusError::HttpResponseTransferCoding:
  case ProxyStatusError::HttpResponseContentCoding:
  case ProxyStatusError::HttpUpgradeFailed:
  case ProxyStatusError::HttpProtocolError:
  case ProxyStatusError::ProxyLoopDetected:
    return Http::Code::BadGateway; // 502
  case ProxyStatusError::DestinationNotFound:
  case ProxyStatusError::ProxyInternalError:
  case ProxyStatusError::ProxyConfigurationError:
    return Http::Code::InternalServerError; // 500
  case ProxyStatusError::DestinationUnavailable:
  case ProxyStatusError::ConnectionLimitReached:
    return Http::Code::ServiceUnavailable; // 503
  case ProxyStatusError::HttpRequestDenied:
    return Http::Code::Forbidden; // 403
  case ProxyStatusError::ProxyInternalResponse:
  case ProxyStatusError::HttpRequestError:
  default:
    return absl::nullopt;
  }
}

const std::string ProxyStatusUtils::makeProxyName(
    absl::string_view node_id, absl::string_view server_name,
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        ProxyStatusConfig* proxy_status_config) {
  if (proxy_status_config == nullptr) {
    return std::string(server_name);
  }
  // For the proxy name, the config specified either a preset proxy name or a literal proxy name.
  switch (proxy_status_config->proxy_name_case()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ProxyStatusConfig::ProxyNameCase::kLiteralProxyName: {
    return std::string(proxy_status_config->literal_proxy_name());
  }
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ProxyStatusConfig::ProxyNameCase::kUseNodeId: {
    return std::string(node_id);
  }
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ProxyStatusConfig::ProxyNameCase::PROXY_NAME_NOT_SET:
  default: {
    return std::string(server_name);
  }
  }
}

const std::string ProxyStatusUtils::makeProxyStatusHeader(
    const StreamInfo& stream_info, const ProxyStatusError error, absl::string_view proxy_name,
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        ProxyStatusConfig& proxy_status_config) {
  std::vector<std::string> retval = {};

  retval.push_back(std::string(proxy_name));

  retval.push_back(absl::StrFormat("error=%s", proxyStatusErrorToString(error)));

  if (!proxy_status_config.remove_details() && stream_info.responseCodeDetails().has_value()) {
    std::vector<std::string> details = {};
    details.push_back(stream_info.responseCodeDetails().value());
    if (!proxy_status_config.remove_connection_termination_details() &&
        stream_info.connectionTerminationDetails().has_value()) {
      details.push_back(stream_info.connectionTerminationDetails().value());
    }
    if (!proxy_status_config.remove_response_flags() && stream_info.hasAnyResponseFlag()) {
      details.push_back(ResponseFlagUtils::toShortString(stream_info));
    }
    retval.push_back(
        absl::StrFormat("details=\"%s\"", StringUtil::escape(absl::StrJoin(details, "; "))));
  }

  return absl::StrJoin(retval, "; ");
}

const absl::string_view
ProxyStatusUtils::proxyStatusErrorToString(const ProxyStatusError proxy_status) {
  switch (proxy_status) {
  case ProxyStatusError::DnsTimeout:
    return DNS_TIMEOUT;
  case ProxyStatusError::DnsError:
    return DNS_ERROR;
  case ProxyStatusError::DestinationNotFound:
    return DESTINATION_NOT_FOUND;
  case ProxyStatusError::DestinationUnavailable:
    return DESTINATION_UNAVAILABLE;
  case ProxyStatusError::DestinationIpProhibited:
    return DESTINATION_IP_PROHIBITED;
  case ProxyStatusError::DestinationIpUnroutable:
    return DESTINATION_IP_UNROUTABLE;
  case ProxyStatusError::ConnectionRefused:
    return CONNECTION_REFUSED;
  case ProxyStatusError::ConnectionTerminated:
    return CONNECTION_TERMINATED;
  case ProxyStatusError::ConnectionTimeout:
    return CONNECTION_TIMEOUT;
  case ProxyStatusError::ConnectionReadTimeout:
    return CONNECTION_READ_TIMEOUT;
  case ProxyStatusError::ConnectionWriteTimeout:
    return CONNECTION_WRITE_TIMEOUT;
  case ProxyStatusError::ConnectionLimitReached:
    return CONNECTION_LIMIT_REACHED;
  case ProxyStatusError::TlsProtocolError:
    return TLS_PROTOCOL_ERROR;
  case ProxyStatusError::TlsCertificateError:
    return TLS_CERTIFICATE_ERROR;
  case ProxyStatusError::TlsAlertReceived:
    return TLS_ALERT_RECEIVED;
  case ProxyStatusError::HttpRequestError:
    return HTTP_REQUEST_ERROR;
  case ProxyStatusError::HttpRequestDenied:
    return HTTP_REQUEST_DENIED;
  case ProxyStatusError::HttpResponseIncomplete:
    return HTTP_RESPONSE_INCOMPLETE;
  case ProxyStatusError::HttpResponseHeaderSectionSize:
    return HTTP_RESPONSE_HEADER_SECTION_SIZE;
  case ProxyStatusError::HttpResponseHeaderSize:
    return HTTP_RESPONSE_HEADER_SIZE;
  case ProxyStatusError::HttpResponseBodySize:
    return HTTP_RESPONSE_BODY_SIZE;
  case ProxyStatusError::HttpResponseTrailerSectionSize:
    return HTTP_RESPONSE_TRAILER_SECTION_SIZE;
  case ProxyStatusError::HttpResponseTrailerSize:
    return HTTP_RESPONSE_TRAILER_SIZE;
  case ProxyStatusError::HttpResponseTransferCoding:
    return HTTP_RESPONSE_TRANSFER_CODING;
  case ProxyStatusError::HttpResponseContentCoding:
    return HTTP_RESPONSE_CONTENT_CODING;
  case ProxyStatusError::HttpResponseTimeout:
    return HTTP_RESPONSE_TIMEOUT;
  case ProxyStatusError::HttpUpgradeFailed:
    return HTTP_UPGRADE_FAILED;
  case ProxyStatusError::HttpProtocolError:
    return HTTP_PROTOCOL_ERROR;
  case ProxyStatusError::ProxyInternalResponse:
    return PROXY_INTERNAL_RESPONSE;
  case ProxyStatusError::ProxyInternalError:
    return PROXY_INTERNAL_ERROR;
  case ProxyStatusError::ProxyConfigurationError:
    return PROXY_CONFIGURATION_ERROR;
  case ProxyStatusError::ProxyLoopDetected:
    return PROXY_LOOP_DETECTED;
  default:
    return "-";
  }
}

const absl::optional<ProxyStatusError>
ProxyStatusUtils::fromStreamInfo(const StreamInfo& stream_info) {
  // NB: This mapping from Envoy-specific CoreResponseFlag enum to Proxy-Status
  // error enum is lossy, since CoreResponseFlag is really a bitset of many
  // CoreResponseFlag enums.
  if (stream_info.hasResponseFlag(CoreResponseFlag::FailedLocalHealthCheck)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::NoHealthyUpstream)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamRequestTimeout)) {
    return ProxyStatusError::HttpResponseTimeout;
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.proxy_status_mapping_more_core_response_flags")) {
    if (stream_info.hasResponseFlag(CoreResponseFlag::DurationTimeout)) {
      return ProxyStatusError::ConnectionTimeout;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::LocalReset)) {
      return ProxyStatusError::ConnectionTimeout;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamRemoteReset)) {
      return ProxyStatusError::ConnectionTerminated;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamConnectionFailure)) {
      return ProxyStatusError::ConnectionRefused;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UnauthorizedExternalService)) {
      return ProxyStatusError::ConnectionRefused;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamConnectionTermination)) {
      return ProxyStatusError::ConnectionTerminated;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::OverloadManager)) {
      return ProxyStatusError::ConnectionLimitReached;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::DropOverLoad)) {
      return ProxyStatusError::ConnectionLimitReached;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::FaultInjected)) {
      return ProxyStatusError::HttpRequestError;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::DownstreamConnectionTermination)) {
      return ProxyStatusError::ConnectionTerminated;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::DownstreamRemoteReset)) {
      return ProxyStatusError::ConnectionTerminated;
    }
  } else {
    if (stream_info.hasResponseFlag(CoreResponseFlag::LocalReset)) {
      return ProxyStatusError::ConnectionTimeout;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamRemoteReset)) {
      return ProxyStatusError::ConnectionTerminated;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamConnectionFailure)) {
      return ProxyStatusError::ConnectionRefused;
    } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamConnectionTermination)) {
      return ProxyStatusError::ConnectionTerminated;
    }
  }

  if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamOverflow)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::NoRouteFound)) {
    return ProxyStatusError::DestinationNotFound;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::RateLimited)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::RateLimitServiceError)) {
    return ProxyStatusError::ConnectionLimitReached;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamRetryLimitExceeded)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::StreamIdleTimeout)) {
    return ProxyStatusError::HttpResponseTimeout;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::InvalidEnvoyRequestHeaders)) {
    return ProxyStatusError::HttpRequestError;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::DownstreamProtocolError)) {
    return ProxyStatusError::HttpRequestError;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamMaxStreamDurationReached)) {
    return ProxyStatusError::HttpResponseTimeout;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::NoFilterConfigFound)) {
    return ProxyStatusError::ProxyConfigurationError;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::UpstreamProtocolError)) {
    return ProxyStatusError::HttpProtocolError;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::NoClusterFound)) {
    return ProxyStatusError::DestinationUnavailable;
  } else if (stream_info.hasResponseFlag(CoreResponseFlag::DnsResolutionFailed)) {
    return ProxyStatusError::DnsError;
  } else {
    return absl::nullopt;
  }
}

} // namespace StreamInfo
} // namespace Envoy
