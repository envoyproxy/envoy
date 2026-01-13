#include "source/common/config/metadata.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/utility.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"
#include "source/extensions/dynamic_modules/abi.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

namespace {

using HeadersMapOptConstRef = OptRef<const Http::HeaderMap>;

HeadersMapOptConstRef getHeaderMapByType(DynamicModuleAccessLogContext* context,
                                         envoy_dynamic_module_type_http_header_type header_type) {
  switch (header_type) {
  case envoy_dynamic_module_type_http_header_type_RequestHeader:
    return context->log_context_.requestHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseHeader:
    return context->log_context_.responseHeaders();
  case envoy_dynamic_module_type_http_header_type_ResponseTrailer:
    return context->log_context_.responseTrailers();
  default:
    return {};
  }
}

bool getHeaderValueImpl(HeadersMapOptConstRef map, envoy_dynamic_module_type_module_buffer key,
                        envoy_dynamic_module_type_envoy_buffer* result, size_t index,
                        size_t* optional_size) {
  if (!map.has_value()) {
    *result = {.ptr = nullptr, .length = 0};
    if (optional_size != nullptr) {
      *optional_size = 0;
    }
    return false;
  }
  absl::string_view key_view(key.ptr, key.length);

  // Note: We convert to LowerCaseString which may involve copying. This could be optimized if
  // callers guarantee lowercase keys.
  const auto values = map->get(Envoy::Http::LowerCaseString(key_view));
  if (optional_size != nullptr) {
    *optional_size = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

bool getHeadersImpl(HeadersMapOptConstRef map,
                    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  if (!map) {
    return false;
  }
  size_t i = 0;
  map->iterate([&i, &result_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

// Helper to convert MonotonicTime to nanoseconds duration from start time.
int64_t monotonicTimeToNanos(const absl::optional<MonotonicTime>& time,
                             const MonotonicTime& start_time) {
  if (!time.has_value()) {
    return -1;
  }
  return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() - start_time).count();
}

} // namespace

extern "C" {

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Headers
// -----------------------------------------------------------------------------

size_t envoy_dynamic_module_callback_access_logger_get_headers_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  HeadersMapOptConstRef map = getHeaderMapByType(context, header_type);
  return map.has_value() ? map->size() : 0;
}

bool envoy_dynamic_module_callback_access_logger_get_headers(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return getHeadersImpl(getHeaderMapByType(context, header_type), result_headers);
}

bool envoy_dynamic_module_callback_access_logger_get_header_value(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_http_header_type header_type,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return getHeaderValueImpl(getHeaderMapByType(context, header_type), key, result, index,
                            total_count_out);
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Stream Info Basic
// -----------------------------------------------------------------------------

uint32_t envoy_dynamic_module_callback_access_logger_get_response_code(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return context->stream_info_.responseCode().value_or(0);
}

bool envoy_dynamic_module_callback_access_logger_get_response_code_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  if (!context->stream_info_.responseCodeDetails().has_value()) {
    return false;
  }
  const auto& details = context->stream_info_.responseCodeDetails().value();
  *result = {const_cast<char*>(details.data()), details.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_has_response_flag(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_response_flag flag) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  // Convert ABI flag to Envoy flag. The enum values are expected to match CoreResponseFlag.
  return context->stream_info_.hasResponseFlag(
      StreamInfo::ResponseFlag(static_cast<StreamInfo::CoreResponseFlag>(flag)));
}

uint64_t envoy_dynamic_module_callback_access_logger_get_response_flags(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return context->stream_info_.legacyResponseFlags();
}

bool envoy_dynamic_module_callback_access_logger_get_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  if (!context->stream_info_.protocol().has_value()) {
    return false;
  }
  const auto& protocol_str =
      Http::Utility::getProtocolString(context->stream_info_.protocol().value());
  *result = {const_cast<char*>(protocol_str.data()), protocol_str.size()};
  return true;
}

void envoy_dynamic_module_callback_access_logger_get_timing_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_timing_info* timing_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& info = context->stream_info_;
  const MonotonicTime start_time = info.startTimeMonotonic();

  timing_out->start_time_unix_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(info.startTime().time_since_epoch())
          .count();

  auto duration = info.requestComplete();
  timing_out->request_complete_duration_ns = duration.has_value() ? duration->count() : -1;

  // Downstream timing.
  const auto downstream = info.downstreamTiming();
  if (downstream.has_value()) {
    timing_out->first_downstream_tx_byte_sent_ns =
        monotonicTimeToNanos(downstream->firstDownstreamTxByteSent(), start_time);
    timing_out->last_downstream_tx_byte_sent_ns =
        monotonicTimeToNanos(downstream->lastDownstreamTxByteSent(), start_time);
  } else {
    timing_out->first_downstream_tx_byte_sent_ns = -1;
    timing_out->last_downstream_tx_byte_sent_ns = -1;
  }

  // Upstream timing.
  const auto upstream = info.upstreamInfo();
  if (upstream.has_value()) {
    const auto& upstream_timing = upstream->upstreamTiming();
    timing_out->first_upstream_tx_byte_sent_ns =
        monotonicTimeToNanos(upstream_timing.first_upstream_tx_byte_sent_, start_time);
    timing_out->last_upstream_tx_byte_sent_ns =
        monotonicTimeToNanos(upstream_timing.last_upstream_tx_byte_sent_, start_time);
    timing_out->first_upstream_rx_byte_received_ns =
        monotonicTimeToNanos(upstream_timing.first_upstream_rx_byte_received_, start_time);
    timing_out->last_upstream_rx_byte_received_ns =
        monotonicTimeToNanos(upstream_timing.last_upstream_rx_byte_received_, start_time);
  } else {
    timing_out->first_upstream_tx_byte_sent_ns = -1;
    timing_out->last_upstream_tx_byte_sent_ns = -1;
    timing_out->first_upstream_rx_byte_received_ns = -1;
    timing_out->last_upstream_rx_byte_received_ns = -1;
  }
}

void envoy_dynamic_module_callback_access_logger_get_bytes_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_bytes_info* bytes_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& info = context->stream_info_;
  bytes_out->bytes_received = info.bytesReceived();
  bytes_out->bytes_sent = info.bytesSent();

  const auto& upstream = info.getUpstreamBytesMeter();
  if (upstream) {
    bytes_out->wire_bytes_received = upstream->wireBytesReceived();
    bytes_out->wire_bytes_sent = upstream->wireBytesSent();
  } else {
    bytes_out->wire_bytes_received = 0;
    bytes_out->wire_bytes_sent = 0;
  }
}

bool envoy_dynamic_module_callback_access_logger_get_route_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& name = context->stream_info_.getRouteName();
  if (name.empty()) {
    return false;
  }
  *result = {const_cast<char*>(name.data()), name.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_is_health_check(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return context->stream_info_.healthCheck();
}

uint32_t envoy_dynamic_module_callback_access_logger_get_attempt_count(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  return context->stream_info_.attemptCount().value_or(0);
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Address Information
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.remoteAddress() || provider.remoteAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.remoteAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.localAddress() || provider.localAddress()->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = provider.localAddress()->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto upstream = context->stream_info_.upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamHost() || !upstream->upstreamHost()->address()) {
    return false;
  }

  const auto& address = upstream->upstreamHost()->address();
  if (address->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = address->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* address_out, uint32_t* port_out) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto upstream = context->stream_info_.upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamLocalAddress()) {
    return false;
  }

  const auto& address = upstream->upstreamLocalAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return false;
  }

  const auto& ip = address->ip();
  const std::string& addr_str = ip->addressAsString();
  *address_out = {const_cast<char*>(addr_str.data()), addr_str.size()};
  *port_out = ip->port();
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Upstream Info
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_upstream_cluster(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  // upstreamClusterInfo is on StreamInfo, not UpstreamInfo.
  const auto cluster_info = context->stream_info_.upstreamClusterInfo();
  if (!cluster_info.has_value() || cluster_info.value() == nullptr) {
    return false;
  }

  const auto& name = cluster_info.value()->name();
  *result = {const_cast<char*>(name.data()), name.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_host(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto upstream = context->stream_info_.upstreamInfo();
  if (!upstream.has_value() || !upstream->upstreamHost()) {
    return false;
  }

  const auto& hostname = upstream->upstreamHost()->hostname();
  *result = {const_cast<char*>(hostname.data()), hostname.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto upstream = context->stream_info_.upstreamInfo();
  if (!upstream.has_value() || upstream->upstreamTransportFailureReason().empty()) {
    return false;
  }

  const auto& reason = upstream->upstreamTransportFailureReason();
  *result = {const_cast<char*>(reason.data()), reason.size()};
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Connection/TLS Info
// -----------------------------------------------------------------------------

uint64_t envoy_dynamic_module_callback_access_logger_get_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.connectionID().has_value()) {
    return 0;
  }
  return provider.connectionID().value();
}

bool envoy_dynamic_module_callback_access_logger_is_mtls(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }
  return provider.sslConnection()->peerCertificateValidated();
}

bool envoy_dynamic_module_callback_access_logger_get_requested_server_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  const auto& sni = provider.requestedServerName();
  if (sni.empty()) {
    return false;
  }
  *result = {const_cast<char*>(sni.data()), sni.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& version = provider.sslConnection()->tlsVersion();
  *result = {const_cast<char*>(version.data()), version.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& subject = provider.sslConnection()->subjectPeerCertificate();
  if (subject.empty()) {
    return false;
  }
  *result = {const_cast<char*>(subject.data()), subject.size()};
  return true;
}

bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto& provider = context->stream_info_.downstreamAddressProvider();
  if (!provider.sslConnection()) {
    return false;
  }

  const std::string& digest = provider.sslConnection()->sha256PeerCertificateDigest();
  if (digest.empty()) {
    return false;
  }
  *result = {const_cast<char*>(digest.data()), digest.size()};
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Metadata and Dynamic State
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  std::string filter_name_str(filter_name.ptr, filter_name.length);
  std::string path_str(path.ptr, path.length);
  std::vector<std::string> path_parts = absl::StrSplit(path_str, '.');

  const auto& metadata = context->stream_info_.dynamicMetadata();
  const auto& value =
      Envoy::Config::Metadata::metadataValue(&metadata, filter_name_str, path_parts);

  if (value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    return false;
  }

  // Note: Currently only string values are supported. Complex types would require serialization
  // to a buffer, but the ABI uses zero-copy pointers to Envoy memory.
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    const auto& str = value.string_value();
    *result = {const_cast<char*>(str.data()), str.size()};
    return true;
  }

  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_filter_state(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: FilterState access is not currently supported. FilterState serialization requires
  // allocation, but the ABI uses zero-copy pointers.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(key);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_request_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  const auto provider = context->stream_info_.getStreamIdProvider();
  if (provider.has_value() && provider->toStringView().has_value()) {
    absl::string_view view = provider->toStringView().value();
    *result = {const_cast<char*>(view.data()), view.size()};
    return true;
  }
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_local_reply_body(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  absl::string_view body = context->log_context_.localReplyBody();
  if (body.empty()) {
    return false;
  }
  *result = {const_cast<char*>(body.data()), body.size()};
  return true;
}

// -----------------------------------------------------------------------------
// Access Logger Callbacks - Tracing
// -----------------------------------------------------------------------------

bool envoy_dynamic_module_callback_access_logger_get_trace_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: Tracing span access is not currently supported. The Span interface doesn't expose
  // trace/span IDs in a way that allows zero-copy access.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_get_span_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  // Note: Tracing span access is not currently supported. The Span interface doesn't expose
  // trace/span IDs in a way that allows zero-copy access.
  UNREFERENCED_PARAMETER(logger_envoy_ptr);
  UNREFERENCED_PARAMETER(result);
  return false;
}

bool envoy_dynamic_module_callback_access_logger_is_trace_sampled(
    envoy_dynamic_module_type_access_logger_envoy_ptr logger_envoy_ptr) {
  auto* context = static_cast<DynamicModuleAccessLogContext*>(logger_envoy_ptr);
  // Note: The Span interface doesn't expose a sampled() method. We check trace reason instead.
  return context->stream_info_.traceReason() != Tracing::Reason::NotTraceable;
}

// -----------------------------------------------------------------------------
// Metrics Callbacks
// -----------------------------------------------------------------------------

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* counter_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Counter& c = Stats::Utility::counterFromStatNames(*config->stats_scope_, {main_stat_name});
  *counter_id_ptr = config->addCounter({c});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto counter = config->getCounterById(id);
  if (!counter.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  counter->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Gauge& g = Stats::Utility::gaugeFromStatNames(*config->stats_scope_, {main_stat_name},
                                                       Stats::Gauge::ImportMode::Accumulate);
  *gauge_id_ptr = config->addGauge({g});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_access_logger_set_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->set(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->add(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_decrement_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto gauge = config->getGaugeById(id);
  if (!gauge.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  gauge->sub(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_histogram(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* histogram_id_ptr) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  Stats::StatName main_stat_name =
      config->stat_name_pool_.add(absl::string_view(name.ptr, name.length));
  Stats::Histogram& h = Stats::Utility::histogramFromStatNames(
      *config->stats_scope_, {main_stat_name}, Stats::Histogram::Unit::Unspecified);
  *histogram_id_ptr = config->addHistogram({h});
  return envoy_dynamic_module_type_metrics_result_Success;
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_record_histogram_value(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr config_envoy_ptr, size_t id,
    uint64_t value) {
  auto* config = static_cast<DynamicModuleAccessLogConfig*>(config_envoy_ptr);
  auto histogram = config->getHistogramById(id);
  if (!histogram.has_value()) {
    return envoy_dynamic_module_type_metrics_result_MetricNotFound;
  }
  histogram->recordValue(value);
  return envoy_dynamic_module_type_metrics_result_Success;
}

} // extern "C"

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
