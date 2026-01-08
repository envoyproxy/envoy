#include "source/extensions/access_loggers/open_telemetry/http_access_log_impl.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/headers.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/open_telemetry/otlp_log_utils.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

HttpAccessLoggerImpl::HttpAccessLoggerImpl(
    Upstream::ClusterManager& cluster_manager,
    const envoy::config::core::v3::HttpService& http_service,
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : cluster_manager_(cluster_manager), http_service_(http_service),
      buffer_flush_interval_(getBufferFlushInterval(config)),
      max_buffer_size_bytes_(getBufferSizeBytes(config)),
      stats_({ALL_OTLP_ACCESS_LOG_STATS(POOL_COUNTER_PREFIX(
          scope, absl::StrCat(OtlpAccessLogStatsPrefix, config.stat_prefix())))}) {

  // Prepares and stores headers to be used later on each export request.
  for (const auto& header_value_option : http_service_.request_headers_to_add()) {
    parsed_headers_to_add_.push_back({Http::LowerCaseString(header_value_option.header().key()),
                                      header_value_option.header().value()});
  }

  root_ = initOtlpMessageRoot(message_, config, local_info);

  // Sets up the flush timer.
  flush_timer_ = dispatcher.createTimer([this]() {
    flush();
    flush_timer_->enableTimer(buffer_flush_interval_);
  });
  flush_timer_->enableTimer(buffer_flush_interval_);
}

void HttpAccessLoggerImpl::log(opentelemetry::proto::logs::v1::LogRecord&& entry) {
  approximate_message_size_bytes_ += entry.ByteSizeLong();
  batched_log_entries_++;
  root_->mutable_log_records()->Add(std::move(entry));

  if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    flush();
  }
}

void HttpAccessLoggerImpl::flush() {
  if (root_->log_records().empty()) {
    return;
  }

  std::string request_body;
  const auto ok = message_.SerializeToString(&request_body);
  if (!ok) {
    ENVOY_LOG(warn, "Error while serializing the binary proto ExportLogsServiceRequest.");
    root_->clear_log_records();
    approximate_message_size_bytes_ = 0;
    return;
  }

  const auto thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(http_service_.http_uri().cluster());
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "OTLP HTTP access log exporter failed: [cluster = {}] is not configured",
              http_service_.http_uri().cluster());
    root_->clear_log_records();
    approximate_message_size_bytes_ = 0;
    return;
  }

  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(http_service_.http_uri());

  // The request follows the OTLP HTTP specification:
  // https://github.com/open-telemetry/opentelemetry-proto/blob/v1.9.0/docs/specification.md#otlphttp.
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);

  // User-Agent header follows the OTLP specification.
  message->headers().setReferenceUserAgent(getOtlpUserAgentHeader());

  // Adds all custom headers to the request.
  for (const auto& header_pair : parsed_headers_to_add_) {
    message->headers().setReference(header_pair.first, header_pair.second);
  }
  message->body().add(request_body);

  const auto options =
      Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(http_service_.http_uri().timeout())))
          .setDiscardResponseBody(true);

  Http::AsyncClient::Request* in_flight_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);

  if (in_flight_request != nullptr) {
    active_requests_.add(*in_flight_request);
    in_flight_log_entries_ = batched_log_entries_;
  } else {
    stats_.logs_dropped_.add(batched_log_entries_);
  }

  root_->clear_log_records();
  approximate_message_size_bytes_ = 0;
  batched_log_entries_ = 0;
}

void HttpAccessLoggerImpl::onSuccess(const Http::AsyncClient::Request& request,
                                     Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  const auto response_code = Http::Utility::getResponseStatus(http_response->headers());
  if (response_code == enumToInt(Http::Code::OK)) {
    stats_.logs_written_.add(in_flight_log_entries_);
  } else {
    ENVOY_LOG(error,
              "OTLP HTTP access log exporter received a non-success status code: {} while "
              "exporting the OTLP message",
              response_code);
    stats_.logs_dropped_.add(in_flight_log_entries_);
  }
  in_flight_log_entries_ = 0;
}

void HttpAccessLoggerImpl::onFailure(const Http::AsyncClient::Request& request,
                                     Http::AsyncClient::FailureReason reason) {
  active_requests_.remove(request);
  ENVOY_LOG(warn, "OTLP HTTP access log export request failed. Failure reason: {}",
            enumToInt(reason));
  stats_.logs_dropped_.add(in_flight_log_entries_);
  in_flight_log_entries_ = 0;
}

HttpAccessLoggerCacheImpl::HttpAccessLoggerCacheImpl(Upstream::ClusterManager& cluster_manager,
                                                     Stats::Scope& scope,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : cluster_manager_(cluster_manager), scope_(scope), tls_slot_(tls.allocateSlot()),
      local_info_(local_info) {
  tls_slot_->set(
      [](Event::Dispatcher& dispatcher) { return std::make_shared<ThreadLocalCache>(dispatcher); });
}

HttpAccessLoggerImpl::SharedPtr HttpAccessLoggerCacheImpl::getOrCreateLogger(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    const envoy::config::core::v3::HttpService& http_service) {
  auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
  const std::size_t config_hash = MessageUtil::hash(config) ^ MessageUtil::hash(http_service);

  const auto it = cache.access_loggers_.find(config_hash);
  if (it != cache.access_loggers_.end()) {
    return it->second;
  }

  auto logger = std::make_shared<HttpAccessLoggerImpl>(cluster_manager_, http_service, config,
                                                       cache.dispatcher_, local_info_, scope_);
  cache.access_loggers_.emplace(config_hash, logger);
  return logger;
}

HttpAccessLog::ThreadLocalLogger::ThreadLocalLogger(HttpAccessLoggerImpl::SharedPtr logger)
    : logger_(std::move(logger)) {}

HttpAccessLog::HttpAccessLog(
    ::Envoy::AccessLog::FilterPtr&& filter,
    envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config,
    ThreadLocal::SlotAllocator& tls, HttpAccessLoggerCacheSharedPtr access_logger_cache,
    const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)), tls_slot_(tls.allocateSlot()),
      access_logger_cache_(std::move(access_logger_cache)), http_service_(config.http_service()),
      filter_state_objects_to_log_(getFilterStateObjectsToLog(config)),
      custom_tags_(getCustomTags(config)) {

  tls_slot_->set([this, config](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalLogger>(
        access_logger_cache_->getOrCreateLogger(config, http_service_));
  });

  // Packs the body "AnyValue" to a "KeyValueList" only if it's not empty. Otherwise the
  // formatter would fail to parse it.
  if (config.body().value_case() != ::opentelemetry::proto::common::v1::AnyValue::VALUE_NOT_SET) {
    body_formatter_ = std::make_unique<OpenTelemetryFormatter>(packBody(config.body()), commands);
  }
  attributes_formatter_ = std::make_unique<OpenTelemetryFormatter>(config.attributes(), commands);
}

void HttpAccessLog::emitLog(const Formatter::Context& log_context,
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
