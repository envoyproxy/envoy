#include "source/extensions/access_loggers/open_telemetry/otlp_log_utils.h"

#include <chrono>
#include <string>

#include "envoy/data/accesslog/v3/accesslog.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

opentelemetry::proto::common::v1::KeyValue getStringKeyValue(const std::string& key,
                                                             const std::string& value) {
  opentelemetry::proto::common::v1::KeyValue keyValue;
  keyValue.set_key(key);
  keyValue.mutable_value()->set_string_value(value);
  return keyValue;
}

::opentelemetry::proto::common::v1::KeyValueList
packBody(const ::opentelemetry::proto::common::v1::AnyValue& body) {
  ::opentelemetry::proto::common::v1::KeyValueList output;
  auto* kv = output.add_values();
  kv->set_key(std::string(BodyKey));
  *kv->mutable_value() = body;
  return output;
}

::opentelemetry::proto::common::v1::AnyValue
unpackBody(const ::opentelemetry::proto::common::v1::KeyValueList& value) {
  ASSERT(value.values().size() == 1 && value.values(0).key() == BodyKey);
  return value.values(0).value();
}

// User-Agent header follows the OTLP specification:
// https://github.com/open-telemetry/opentelemetry-specification/blob/v1.52.0/specification/protocol/exporter.md#user-agent
const std::string& getOtlpUserAgentHeader() {
  CONSTRUCT_ON_FIRST_USE(std::string, "OTel-OTLP-Exporter-Envoy/" + VersionInfo::version());
}

void populateTraceContext(opentelemetry::proto::logs::v1::LogRecord& log_entry,
                          const std::string& trace_id_hex, const std::string& span_id_hex) {
  // Sets trace_id if available. OpenTelemetry trace_id is a 16-byte array, and backends
  // (e.g. OTel-collector) will reject requests if the length is incorrect. Some trace
  // providers (e.g. Zipkin) return a 64-bit hex string, which must be padded to 128-bit.
  if (trace_id_hex.size() == TraceIdHexLength) {
    *log_entry.mutable_trace_id() = absl::HexStringToBytes(trace_id_hex);
  } else if (trace_id_hex.size() == ShortTraceIdHexLength) {
    const auto trace_id = absl::StrCat(Hex::uint64ToHex(0), trace_id_hex);
    *log_entry.mutable_trace_id() = absl::HexStringToBytes(trace_id);
  }
  // Sets span_id if available.
  if (!span_id_hex.empty()) {
    *log_entry.mutable_span_id() = absl::HexStringToBytes(span_id_hex);
  }
}

const std::string& getLogName(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  // Prefer top-level log_name, fall back to common_config.log_name (deprecated).
  if (!config.log_name().empty()) {
    return config.log_name();
  }
  return config.common_config().log_name();
}

const envoy::config::core::v3::GrpcService& getGrpcService(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  // Prefer top-level grpc_service, fall back to common_config.grpc_service (deprecated).
  if (config.has_grpc_service()) {
    return config.grpc_service();
  }
  return config.common_config().grpc_service();
}

std::chrono::milliseconds getBufferFlushInterval(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  if (config.has_buffer_flush_interval()) {
    return std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(config.buffer_flush_interval()));
  }
  return DefaultBufferFlushInterval;
}

uint64_t getBufferSizeBytes(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  if (config.has_buffer_size_bytes()) {
    return config.buffer_size_bytes().value();
  }
  return DefaultMaxBufferSizeBytes;
}

std::vector<std::string> getFilterStateObjectsToLog(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  return std::vector<std::string>(config.filter_state_objects_to_log().begin(),
                                  config.filter_state_objects_to_log().end());
}

std::vector<Tracing::CustomTagConstSharedPtr> getCustomTags(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config) {
  std::vector<Tracing::CustomTagConstSharedPtr> custom_tags;
  for (const auto& custom_tag : config.custom_tags()) {
    custom_tags.push_back(Tracing::CustomTagUtility::createCustomTag(custom_tag));
  }
  return custom_tags;
}

void addFilterStateToAttributes(const StreamInfo::StreamInfo& stream_info,
                                const std::vector<std::string>& filter_state_objects_to_log,
                                opentelemetry::proto::logs::v1::LogRecord& log_entry) {
  for (const auto& key : filter_state_objects_to_log) {
    const StreamInfo::FilterState* filter_state = &stream_info.filterState();
    // Check downstream filter state first, then upstream.
    if (auto state = filter_state->getDataReadOnlyGeneric(key); state != nullptr) {
      ProtobufTypes::MessagePtr serialized_proto = state->serializeAsProto();
      if (serialized_proto != nullptr) {
        auto json_or_error = MessageUtil::getJsonStringFromMessage(*serialized_proto);
        if (json_or_error.ok()) {
          auto* attr = log_entry.add_attributes();
          attr->set_key(key);
          attr->mutable_value()->set_string_value(json_or_error.value());
        }
      }
    } else if (stream_info.upstreamInfo().has_value() &&
               stream_info.upstreamInfo()->upstreamFilterState() != nullptr) {
      if (auto upstream_state =
              stream_info.upstreamInfo()->upstreamFilterState()->getDataReadOnlyGeneric(key);
          upstream_state != nullptr) {
        ProtobufTypes::MessagePtr serialized_proto = upstream_state->serializeAsProto();
        if (serialized_proto != nullptr) {
          auto json_or_error = MessageUtil::getJsonStringFromMessage(*serialized_proto);
          if (json_or_error.ok()) {
            auto* attr = log_entry.add_attributes();
            attr->set_key(key);
            attr->mutable_value()->set_string_value(json_or_error.value());
          }
        }
      }
    }
  }
}

void addCustomTagsToAttributes(const std::vector<Tracing::CustomTagConstSharedPtr>& custom_tags,
                               const Formatter::Context& context,
                               const StreamInfo::StreamInfo& stream_info,
                               opentelemetry::proto::logs::v1::LogRecord& log_entry) {
  if (custom_tags.empty()) {
    return;
  }

  // Create empty header map if request headers not available.
  const Http::RequestHeaderMap* headers_ptr =
      context.requestHeaders().has_value()
          ? &static_cast<const Http::RequestHeaderMap&>(context.requestHeaders().value())
          : Http::StaticEmptyHeaders::get().request_headers.get();
  const Http::RequestHeaderMap& headers = *headers_ptr;

  Tracing::ReadOnlyHttpTraceContext trace_context(headers);
  Tracing::CustomTagContext tag_context{trace_context, stream_info, context};

  // Use a temporary AccessLogCommon to extract custom tag values via applyLog.
  envoy::data::accesslog::v3::AccessLogCommon temp_log;
  for (const auto& custom_tag : custom_tags) {
    custom_tag->applyLog(temp_log, tag_context);
  }

  // Copy custom tags to OTLP attributes.
  for (const auto& [key, value] : temp_log.custom_tags()) {
    auto* attr = log_entry.add_attributes();
    attr->set_key(key);
    attr->mutable_value()->set_string_value(value);
  }
}

opentelemetry::proto::logs::v1::ScopeLogs* initOtlpMessageRoot(
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest& message,
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    const LocalInfo::LocalInfo& local_info) {
  auto* resource_logs = message.add_resource_logs();
  auto* root = resource_logs->add_scope_logs();
  auto* resource = resource_logs->mutable_resource();
  if (!config.disable_builtin_labels()) {
    *resource->add_attributes() = getStringKeyValue("log_name", getLogName(config));
    *resource->add_attributes() = getStringKeyValue("zone_name", local_info.zoneName());
    *resource->add_attributes() = getStringKeyValue("cluster_name", local_info.clusterName());
    *resource->add_attributes() = getStringKeyValue("node_name", local_info.nodeName());
  }
  for (const auto& pair : config.resource_attributes().values()) {
    *resource->add_attributes() = pair;
  }
  return root;
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
