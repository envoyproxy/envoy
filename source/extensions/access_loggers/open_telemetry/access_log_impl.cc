#include "source/extensions/access_loggers/open_telemetry/access_log_impl.h"

#include <chrono>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"

#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/headers.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/open_telemetry/otlp_log_utils.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle(Http::CustomHeaders::get().Referer);

AccessLog::ThreadLocalLogger::ThreadLocalLogger(GrpcAccessLoggerSharedPtr logger)
    : logger_(std::move(logger)) {}

AccessLog::AccessLog(
    ::Envoy::AccessLog::FilterPtr&& filter,
    envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config,
    ThreadLocal::SlotAllocator& tls, GrpcAccessLoggerCacheSharedPtr access_logger_cache,
    const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)), tls_slot_(tls.allocateSlot()),
      access_logger_cache_(std::move(access_logger_cache)),
      filter_state_objects_to_log_(getFilterStateObjectsToLog(config)),
      custom_tags_(getCustomTags(config)) {

  THROW_IF_NOT_OK(Envoy::Config::Utility::checkTransportVersion(config.common_config()));
  tls_slot_->set([this, config](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalLogger>(
        access_logger_cache_->getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP));
  });

  // Packing the body "AnyValue" to a "KeyValueList" only if it's not empty, otherwise the
  // formatter would fail to parse it.
  if (config.body().value_case() != ::opentelemetry::proto::common::v1::AnyValue::VALUE_NOT_SET) {
    body_formatter_ = std::make_unique<OpenTelemetryFormatter>(packBody(config.body()), commands);
  }
  attributes_formatter_ = std::make_unique<OpenTelemetryFormatter>(config.attributes(), commands);
}

void AccessLog::emitLog(const Formatter::Context& log_context,
                        const StreamInfo::StreamInfo& stream_info) {
  opentelemetry::proto::logs::v1::LogRecord log_entry;
  log_entry.set_time_unix_nano(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   stream_info.startTime().time_since_epoch())
                                   .count());

  // Unpacks the body "KeyValueList" to "AnyValue".
  if (body_formatter_) {
    const auto formatted_body = unpackBody(body_formatter_->format(log_context, stream_info));
    *log_entry.mutable_body() = formatted_body;
  }
  const auto formatted_attributes = attributes_formatter_->format(log_context, stream_info);
  *log_entry.mutable_attributes() = formatted_attributes.values();

  // Sets trace context (trace_id, span_id) if available.
  const std::string trace_id_hex =
      log_context.activeSpan().has_value() ? log_context.activeSpan()->getTraceId() : "";
  const std::string span_id_hex =
      log_context.activeSpan().has_value() ? log_context.activeSpan()->getSpanId() : "";
  populateTraceContext(log_entry, trace_id_hex, span_id_hex);

  addFilterStateToAttributes(stream_info, filter_state_objects_to_log_, log_entry);
  addCustomTagsToAttributes(custom_tags_, log_context, stream_info, log_entry);

  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(std::move(log_entry));
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
