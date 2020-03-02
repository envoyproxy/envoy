#include "extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "common/common/assert.h"
#include "common/network/utility.h"
#include "common/stream_info/utility.h"

#include "extensions/access_loggers/grpc/grpc_access_log_utils.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

HttpGrpcAccessLog::ThreadLocalLogger::ThreadLocalLogger(
    GrpcCommon::GrpcAccessLoggerSharedPtr logger)
    : logger_(std::move(logger)) {}

HttpGrpcAccessLog::HttpGrpcAccessLog(
    AccessLog::FilterPtr&& filter,
    envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig config,
    ThreadLocal::SlotAllocator& tls, GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache)
    : Common::ImplBase(std::move(filter)), config_(std::move(config)),
      tls_slot_(tls.allocateSlot()), access_logger_cache_(std::move(access_logger_cache)) {
  for (const auto& header : config_.additional_request_headers_to_log()) {
    request_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_.additional_response_headers_to_log()) {
    response_headers_to_log_.emplace_back(header);
  }

  for (const auto& header : config_.additional_response_trailers_to_log()) {
    response_trailers_to_log_.emplace_back(header);
  }

  tls_slot_->set([this](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalLogger>(access_logger_cache_->getOrCreateLogger(
        config_.common_config(), GrpcCommon::GrpcAccessLoggerType::HTTP));
  });
}

void HttpGrpcAccessLog::emitLog(const Http::RequestHeaderMap& request_headers,
                                const Http::ResponseHeaderMap& response_headers,
                                const Http::ResponseTrailerMap& response_trailers,
                                const StreamInfo::StreamInfo& stream_info) {
  // Common log properties.
  // TODO(mattklein123): Populate sample_rate field.
  envoy::data::accesslog::v3::HTTPAccessLogEntry log_entry;
  GrpcCommon::Utility::extractCommonAccessLogProperties(*log_entry.mutable_common_properties(),
                                                        stream_info, config_.common_config());

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
    request_properties->set_scheme(std::string(request_headers.Scheme()->value().getStringView()));
  }
  if (request_headers.Host() != nullptr) {
    request_properties->set_authority(std::string(request_headers.Host()->value().getStringView()));
  }
  if (request_headers.Path() != nullptr) {
    request_properties->set_path(std::string(request_headers.Path()->value().getStringView()));
  }
  if (request_headers.UserAgent() != nullptr) {
    request_properties->set_user_agent(
        std::string(request_headers.UserAgent()->value().getStringView()));
  }
  if (request_headers.Referer() != nullptr) {
    request_properties->set_referer(
        std::string(request_headers.Referer()->value().getStringView()));
  }
  if (request_headers.ForwardedFor() != nullptr) {
    request_properties->set_forwarded_for(
        std::string(request_headers.ForwardedFor()->value().getStringView()));
  }
  if (request_headers.RequestId() != nullptr) {
    request_properties->set_request_id(
        std::string(request_headers.RequestId()->value().getStringView()));
  }
  if (request_headers.EnvoyOriginalPath() != nullptr) {
    request_properties->set_original_path(
        std::string(request_headers.EnvoyOriginalPath()->value().getStringView()));
  }
  request_properties->set_request_headers_bytes(request_headers.byteSize());
  request_properties->set_request_body_bytes(stream_info.bytesReceived());
  if (request_headers.Method() != nullptr) {
    envoy::config::core::v3::RequestMethod method = envoy::config::core::v3::METHOD_UNSPECIFIED;
    envoy::config::core::v3::RequestMethod_Parse(
        std::string(request_headers.Method()->value().getStringView()), &method);
    request_properties->set_request_method(method);
  }
  if (!request_headers_to_log_.empty()) {
    auto* logged_headers = request_properties->mutable_request_headers();

    for (const auto& header : request_headers_to_log_) {
      const Http::HeaderEntry* entry = request_headers.get(header);
      if (entry != nullptr) {
        logged_headers->insert({header.get(), std::string(entry->value().getStringView())});
      }
    }
  }

  // HTTP response properties.
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
      const Http::HeaderEntry* entry = response_headers.get(header);
      if (entry != nullptr) {
        logged_headers->insert({header.get(), std::string(entry->value().getStringView())});
      }
    }
  }

  if (!response_trailers_to_log_.empty()) {
    auto* logged_headers = response_properties->mutable_response_trailers();

    for (const auto& header : response_trailers_to_log_) {
      const Http::HeaderEntry* entry = response_trailers.get(header);
      if (entry != nullptr) {
        logged_headers->insert({header.get(), std::string(entry->value().getStringView())});
      }
    }
  }

  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(std::move(log_entry));
}

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
