#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/hex.h"
#include "source/common/tracing/custom_tag_impl.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Default buffer flush interval (1 second).
constexpr std::chrono::milliseconds DefaultBufferFlushInterval{1000};
// Default max buffer size (16KB).
constexpr uint64_t DefaultMaxBufferSizeBytes = 16384;

// Key used to pack/unpack the body AnyValue to a KeyValueList.
constexpr absl::string_view BodyKey = "body";

// OpenTelemetry trace ID length in hex (128-bit = 32 hex chars).
constexpr size_t TraceIdHexLength = 32;
// Zipkin-style trace ID length in hex (64-bit = 16 hex chars).
constexpr size_t ShortTraceIdHexLength = 16;

constexpr absl::string_view OtlpAccessLogStatsPrefix = "access_logs.open_telemetry_access_log.";

#define ALL_OTLP_ACCESS_LOG_STATS(COUNTER)                                                         \
  COUNTER(logs_written)                                                                            \
  COUNTER(logs_dropped)

struct OtlpAccessLogStats {
  ALL_OTLP_ACCESS_LOG_STATS(GENERATE_COUNTER_STRUCT)
};

// Creates a KeyValue protobuf with a string value.
opentelemetry::proto::common::v1::KeyValue getStringKeyValue(const std::string& key,
                                                             const std::string& value);

// Packs the body "AnyValue" to a "KeyValueList" with a single key.
::opentelemetry::proto::common::v1::KeyValueList
packBody(const ::opentelemetry::proto::common::v1::AnyValue& body);

// Unpacks the body "AnyValue" from a "KeyValueList".
::opentelemetry::proto::common::v1::AnyValue
unpackBody(const ::opentelemetry::proto::common::v1::KeyValueList& value);

// User-Agent header per OTLP specification.
const std::string& getOtlpUserAgentHeader();

// Populates trace context (trace_id, span_id) on a LogRecord.
// Handles 128-bit (32 hex chars) and 64-bit Zipkin-style (16 hex chars) trace IDs.
void populateTraceContext(opentelemetry::proto::logs::v1::LogRecord& log_entry,
                          const std::string& trace_id_hex, const std::string& span_id_hex);

// Returns log_name, with fallback to common_config.log_name.
const std::string& getLogName(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

// Returns grpc_service, with fallback to common_config.grpc_service.
const envoy::config::core::v3::GrpcService& getGrpcService(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

std::chrono::milliseconds getBufferFlushInterval(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

uint64_t getBufferSizeBytes(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

std::vector<std::string> getFilterStateObjectsToLog(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

std::vector<Tracing::CustomTagConstSharedPtr> getCustomTags(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config);

void addFilterStateToAttributes(const StreamInfo::StreamInfo& stream_info,
                                const std::vector<std::string>& filter_state_objects_to_log,
                                opentelemetry::proto::logs::v1::LogRecord& log_entry);

void addCustomTagsToAttributes(const std::vector<Tracing::CustomTagConstSharedPtr>& custom_tags,
                               const Formatter::Context& context,
                               const StreamInfo::StreamInfo& stream_info,
                               opentelemetry::proto::logs::v1::LogRecord& log_entry);

// Initializes the OTLP message root structure with resource attributes.
// Returns a pointer to the ScopeLogs where log records should be added.
opentelemetry::proto::logs::v1::ScopeLogs* initOtlpMessageRoot(
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest& message,
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    const LocalInfo::LocalInfo& local_info);

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
