#include "source/extensions/access_loggers/open_telemetry/access_log_impl.h"

#include <chrono>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/headers.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

// Used to pack/unpack the body AnyValue to a KeyValueList.
const char BODY_KEY[] = "body";

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

namespace {

// Packing the body "AnyValue" to a "KeyValueList" with a single key and the body as value.
::opentelemetry::proto::common::v1::KeyValueList
packBody(const ::opentelemetry::proto::common::v1::AnyValue& body) {
  ::opentelemetry::proto::common::v1::KeyValueList output;
  auto* kv = output.add_values();
  kv->set_key(BODY_KEY);
  *kv->mutable_value() = body;
  return output;
}

::opentelemetry::proto::common::v1::AnyValue
unpackBody(const ::opentelemetry::proto::common::v1::KeyValueList& value) {
  ASSERT(value.values().size() == 1 && value.values(0).key() == BODY_KEY);
  return value.values(0).value();
}

} // namespace

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
      access_logger_cache_(std::move(access_logger_cache)) {

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

void AccessLog::emitLog(const Formatter::HttpFormatterContext& log_context,
                        const StreamInfo::StreamInfo& stream_info) {
  opentelemetry::proto::logs::v1::LogRecord log_entry;
  log_entry.set_time_unix_nano(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   stream_info.startTime().time_since_epoch())
                                   .count());

  // Unpacking the body "KeyValueList" to "AnyValue".
  if (body_formatter_) {
    const auto formatted_body = unpackBody(body_formatter_->format(log_context, stream_info));
    *log_entry.mutable_body() = formatted_body;
  }
  const auto formatted_attributes = attributes_formatter_->format(log_context, stream_info);
  *log_entry.mutable_attributes() = formatted_attributes.values();

  // Setting the trace id if available.
  // OpenTelemetry trace id is a [16]byte array, backend(e.g. OTel-collector) will reject the
  // request if the length is not 16. Some trace provider(e.g. zipkin) may return it as a 64-bit hex
  // string. In this case, we need to convert it to a 128-bit hex string, padding left with zeros.
  std::string trace_id_hex = log_context.activeSpan().getTraceId();
  if (trace_id_hex.size() == 32) {
    *log_entry.mutable_trace_id() = absl::HexStringToBytes(trace_id_hex);
  } else if (trace_id_hex.size() == 16) {
    auto trace_id = absl::StrCat(Hex::uint64ToHex(0), trace_id_hex);
    *log_entry.mutable_trace_id() = absl::HexStringToBytes(trace_id);
  }
  std::string span_id_hex = log_context.activeSpan().getSpanId();
  if (!span_id_hex.empty()) {
    *log_entry.mutable_span_id() = absl::HexStringToBytes(span_id_hex);
  }

  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(std::move(log_entry));
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
