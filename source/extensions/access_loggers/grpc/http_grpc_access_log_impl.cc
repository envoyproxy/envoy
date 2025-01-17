#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/grpc/grpc_access_log_utils.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle(Http::CustomHeaders::get().Referer);

HttpGrpcAccessLog::ThreadLocalLogger::ThreadLocalLogger(
    GrpcCommon::GrpcAccessLoggerSharedPtr logger)
    : logger_(std::move(logger)) {}

HttpGrpcAccessLog::HttpGrpcAccessLog(AccessLog::FilterPtr&& filter,
                                     const HttpGrpcAccessLogConfig config,
                                     ThreadLocal::SlotAllocator& tls,
                                     GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache)
    : Common::ImplBase(std::move(filter)),
      config_(std::make_shared<const HttpGrpcAccessLogConfig>(std::move(config))),
      tls_slot_(tls.allocateSlot()), access_logger_cache_(std::move(access_logger_cache)) {
  for (const auto& header : config_->additional_request_headers_to_log()) {
    request_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_->additional_response_headers_to_log()) {
    response_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_->additional_response_trailers_to_log()) {
    response_trailers_to_log_.emplace_back(header);
  }
  THROW_IF_NOT_OK(Envoy::Config::Utility::checkTransportVersion(config_->common_config()));
  tls_slot_->set(
      [config = config_, access_logger_cache = access_logger_cache_](Event::Dispatcher&) {
        return std::make_shared<ThreadLocalLogger>(access_logger_cache->getOrCreateLogger(
            config->common_config(), Common::GrpcAccessLoggerType::HTTP));
      });
}

void HttpGrpcAccessLog::emitLog(const Formatter::HttpFormatterContext& context,
                                const StreamInfo::StreamInfo& stream_info) {
  // Common log properties.
  // TODO(mattklein123): Populate sample_rate field.
  envoy::data::accesslog::v3::HTTPAccessLogEntry log_entry;

  const auto& request_headers = context.requestHeaders();

  GrpcCommon::Utility::extractCommonAccessLogProperties(
      *log_entry.mutable_common_properties(), request_headers, stream_info,
      config_->common_config(), context.accessLogType());

  if (stream_info.protocol()) {
    switch (stream_info.protocol().value()) {
    case Http::Protocol::Http10:
      log_entry.set_protocol_version(envoy::data::accesslog::v3::HTTPAccessLogEntry::HTTP10);
      break;
    case Http::Protocol::Http11:
      log_entry.set_protocol_version(envoy::data::accesslog::v3::HTTPAccessLogEntry::HTTP11);
      break;
    case Http::Protocol::Http2:
      log_entry.set_protocol_version(envoy::data::accesslog::v3::HTTPAccessLogEntry::HTTP2);
      break;
    case Http::Protocol::Http3:
      log_entry.set_protocol_version(envoy::data::accesslog::v3::HTTPAccessLogEntry::HTTP3);
      break;
    }
  }

  // HTTP request properties.
  // TODO(mattklein123): Populate port field.
  auto* request_properties = log_entry.mutable_request();
  if (request_headers.Scheme() != nullptr) {
    request_properties->set_scheme(
        MessageUtil::sanitizeUtf8String(request_headers.getSchemeValue()));
  }
  if (request_headers.Host() != nullptr) {
    request_properties->set_authority(
        MessageUtil::sanitizeUtf8String(request_headers.getHostValue()));
  }
  if (request_headers.Path() != nullptr) {
    request_properties->set_path(MessageUtil::sanitizeUtf8String(request_headers.getPathValue()));
  }
  if (request_headers.UserAgent() != nullptr) {
    request_properties->set_user_agent(
        MessageUtil::sanitizeUtf8String(request_headers.getUserAgentValue()));
  }
  if (request_headers.getInline(referer_handle.handle()) != nullptr) {
    request_properties->set_referer(
        MessageUtil::sanitizeUtf8String(request_headers.getInlineValue(referer_handle.handle())));
  }
  if (request_headers.ForwardedFor() != nullptr) {
    request_properties->set_forwarded_for(
        MessageUtil::sanitizeUtf8String(request_headers.getForwardedForValue()));
  }
  if (request_headers.RequestId() != nullptr) {
    request_properties->set_request_id(
        MessageUtil::sanitizeUtf8String(request_headers.getRequestIdValue()));
  }
  if (request_headers.EnvoyOriginalPath() != nullptr) {
    request_properties->set_original_path(
        MessageUtil::sanitizeUtf8String(request_headers.getEnvoyOriginalPathValue()));
  }
  request_properties->set_request_headers_bytes(request_headers.byteSize());
  request_properties->set_request_body_bytes(stream_info.bytesReceived());

  if (request_headers.Method() != nullptr) {
    envoy::config::core::v3::RequestMethod method = envoy::config::core::v3::METHOD_UNSPECIFIED;
    envoy::config::core::v3::RequestMethod_Parse(
        MessageUtil::sanitizeUtf8String(request_headers.getMethodValue()), &method);
    request_properties->set_request_method(method);
  }
  if (!request_headers_to_log_.empty()) {
    auto* logged_headers = request_properties->mutable_request_headers();

    for (const auto& header : request_headers_to_log_) {
      const auto all_values = Http::HeaderUtility::getAllOfHeaderAsString(request_headers, header);
      if (all_values.result().has_value()) {
        logged_headers->insert(
            {header.get(), MessageUtil::sanitizeUtf8String(all_values.result().value())});
      }
    }
  }

  // HTTP response properties.
  const auto& response_headers = context.responseHeaders();
  const auto& response_trailers = context.responseTrailers();

  auto* response_properties = log_entry.mutable_response();
  if (stream_info.responseCode()) {
    response_properties->mutable_response_code()->set_value(stream_info.responseCode().value());
  }
  if (stream_info.responseCodeDetails()) {
    response_properties->set_response_code_details(stream_info.responseCodeDetails().value());
  }
  response_properties->set_response_headers_bytes(response_headers.byteSize());
  response_properties->set_response_body_bytes(stream_info.bytesSent());
  if (!response_headers_to_log_.empty()) {
    auto* logged_headers = response_properties->mutable_response_headers();

    for (const auto& header : response_headers_to_log_) {
      const auto all_values = Http::HeaderUtility::getAllOfHeaderAsString(response_headers, header);
      if (all_values.result().has_value()) {
        logged_headers->insert(
            {header.get(), MessageUtil::sanitizeUtf8String(all_values.result().value())});
      }
    }
  }

  if (!response_trailers_to_log_.empty()) {
    auto* logged_headers = response_properties->mutable_response_trailers();

    for (const auto& header : response_trailers_to_log_) {
      const auto all_values =
          Http::HeaderUtility::getAllOfHeaderAsString(response_trailers, header);
      if (all_values.result().has_value()) {
        logged_headers->insert(
            {header.get(), MessageUtil::sanitizeUtf8String(all_values.result().value())});
      }
    }
  }

  if (const auto& bytes_meter = stream_info.getDownstreamBytesMeter(); bytes_meter != nullptr) {
    request_properties->set_downstream_header_bytes_received(bytes_meter->headerBytesReceived());
    response_properties->set_downstream_header_bytes_sent(bytes_meter->headerBytesSent());
  }
  if (const auto& bytes_meter = stream_info.getUpstreamBytesMeter(); bytes_meter != nullptr) {
    request_properties->set_upstream_header_bytes_sent(bytes_meter->headerBytesSent());
    response_properties->set_upstream_header_bytes_received(bytes_meter->headerBytesReceived());
  }

  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(std::move(log_entry));
}

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
