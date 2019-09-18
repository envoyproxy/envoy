#include "common/tracing/http_tracer_impl.h"

#include <string>

#include "common/access_log/access_log_formatter.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/config/metadata.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/runtime/uuid_util.h"
#include "common/stream_info/utility.h"

namespace Envoy {
namespace Tracing {

// TODO(mattklein123) PERF: Avoid string creations/copies in this entire file.
static std::string buildResponseCode(const StreamInfo::StreamInfo& info) {
  return info.responseCode() ? std::to_string(info.responseCode().value()) : "0";
}

static std::string valueOrDefault(const Http::HeaderEntry* header, const char* default_value) {
  return header ? std::string(header->value().getStringView()) : default_value;
}

static std::string buildUrl(const Http::HeaderMap& request_headers,
                            const uint32_t max_path_length) {
  std::string path(request_headers.EnvoyOriginalPath()
                       ? request_headers.EnvoyOriginalPath()->value().getStringView()
                       : request_headers.Path()->value().getStringView());

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{}://{}{}", valueOrDefault(request_headers.ForwardedProto(), ""),
                     valueOrDefault(request_headers.Host(), ""), path);
}

const std::string HttpTracerUtility::IngressOperation = "ingress";
const std::string HttpTracerUtility::EgressOperation = "egress";

const std::string& HttpTracerUtility::toString(OperationName operation_name) {
  switch (operation_name) {
  case OperationName::Ingress:
    return IngressOperation;
  case OperationName::Egress:
    return EgressOperation;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Decision HttpTracerUtility::isTracing(const StreamInfo::StreamInfo& stream_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude health check requests immediately.
  if (stream_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  if (!request_headers.RequestId()) {
    return {Reason::NotTraceableRequestId, false};
  }

  UuidTraceStatus trace_status =
      UuidUtils::isTraceableUuid(request_headers.RequestId()->value().getStringView());

  switch (trace_status) {
  case UuidTraceStatus::Client:
    return {Reason::ClientForced, true};
  case UuidTraceStatus::Forced:
    return {Reason::ServiceForced, true};
  case UuidTraceStatus::Sampled:
    return {Reason::Sampling, true};
  case UuidTraceStatus::NoTrace:
    return {Reason::NotTraceableRequestId, false};
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

static void addGrpcTags(Span& span, const Http::HeaderMap& headers) {
  const Http::HeaderEntry* grpc_status_header = headers.GrpcStatus();
  if (grpc_status_header) {
    span.setTag(Tracing::Tags::get().GrpcStatusCode, grpc_status_header->value().getStringView());
  }
  const Http::HeaderEntry* grpc_message_header = headers.GrpcMessage();
  if (grpc_message_header) {
    span.setTag(Tracing::Tags::get().GrpcMessage, grpc_message_header->value().getStringView());
  }
  absl::optional<Grpc::Status::GrpcStatus> grpc_status_code = Grpc::Common::getGrpcStatus(headers);
  // Set error tag when status is not OK.
  if (grpc_status_code && grpc_status_code.value() != Grpc::Status::GrpcStatus::Ok) {
    span.setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  }
}

static void annotateVerbose(Span& span, const StreamInfo::StreamInfo& stream_info) {
  const auto start_time = stream_info.startTime();
  if (stream_info.lastDownstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.lastDownstreamRxByteReceived()),
             Tracing::Logs::get().LastDownstreamRxByteReceived);
  }
  if (stream_info.firstUpstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.firstUpstreamTxByteSent()),
             Tracing::Logs::get().FirstUpstreamTxByteSent);
  }
  if (stream_info.lastUpstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.lastUpstreamTxByteSent()),
             Tracing::Logs::get().LastUpstreamTxByteSent);
  }
  if (stream_info.firstUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.firstUpstreamRxByteReceived()),
             Tracing::Logs::get().FirstUpstreamRxByteReceived);
  }
  if (stream_info.lastUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.lastUpstreamRxByteReceived()),
             Tracing::Logs::get().LastUpstreamRxByteReceived);
  }
  if (stream_info.firstDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.firstDownstreamTxByteSent()),
             Tracing::Logs::get().FirstDownstreamTxByteSent);
  }
  if (stream_info.lastDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *stream_info.lastDownstreamTxByteSent()),
             Tracing::Logs::get().LastDownstreamTxByteSent);
  }
}

void HttpTracerUtility::finalizeDownstreamSpan(Span& span, const Http::HeaderMap* request_headers,
                                               const Http::HeaderMap* response_headers,
                                               const Http::HeaderMap* response_trailers,
                                               const StreamInfo::StreamInfo& stream_info,
                                               const Config& tracing_config) {
  // Pre response data.
  if (request_headers) {
    if (request_headers->RequestId()) {
      span.setTag(Tracing::Tags::get().GuidXRequestId,
                  std::string(request_headers->RequestId()->value().getStringView()));
    }
    span.setTag(Tracing::Tags::get().HttpUrl,
                buildUrl(*request_headers, tracing_config.maxPathTagLength()));
    span.setTag(Tracing::Tags::get().HttpMethod,
                std::string(request_headers->Method()->value().getStringView()));
    span.setTag(Tracing::Tags::get().DownstreamCluster,
                valueOrDefault(request_headers->EnvoyDownstreamServiceCluster(), "-"));
    span.setTag(Tracing::Tags::get().UserAgent, valueOrDefault(request_headers->UserAgent(), "-"));
    span.setTag(Tracing::Tags::get().HttpProtocol,
                AccessLog::AccessLogFormatUtils::protocolToString(stream_info.protocol()));

    if (request_headers->ClientTraceId()) {
      span.setTag(Tracing::Tags::get().GuidXClientTraceId,
                  std::string(request_headers->ClientTraceId()->value().getStringView()));
    }
  }
  CustomTagContext ctx{request_headers, stream_info};
  for (const CustomTagPtr& custom_tag : tracing_config.customTags()) {
    custom_tag->apply(span, ctx);
  }
  span.setTag(Tracing::Tags::get().RequestSize, std::to_string(stream_info.bytesReceived()));
  span.setTag(Tracing::Tags::get().ResponseSize, std::to_string(stream_info.bytesSent()));

  setCommonTags(span, response_headers, response_trailers, stream_info, tracing_config);

  span.finishSpan();
}

void HttpTracerUtility::finalizeUpstreamSpan(Span& span, const Http::HeaderMap* response_headers,
                                             const Http::HeaderMap* response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             const Config& tracing_config) {
  span.setTag(Tracing::Tags::get().HttpProtocol,
              AccessLog::AccessLogFormatUtils::protocolToString(stream_info.protocol()));

  setCommonTags(span, response_headers, response_trailers, stream_info, tracing_config);

  span.finishSpan();
}

void HttpTracerUtility::setCommonTags(Span& span, const Http::HeaderMap* response_headers,
                                      const Http::HeaderMap* response_trailers,
                                      const StreamInfo::StreamInfo& stream_info,
                                      const Config& tracing_config) {

  span.setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);

  if (nullptr != stream_info.upstreamHost()) {
    span.setTag(Tracing::Tags::get().UpstreamCluster, stream_info.upstreamHost()->cluster().name());
  }

  // Post response data.
  span.setTag(Tracing::Tags::get().HttpStatusCode, buildResponseCode(stream_info));
  span.setTag(Tracing::Tags::get().ResponseFlags,
              StreamInfo::ResponseFlagUtils::toShortString(stream_info));

  // GRPC data.
  if (response_trailers && response_trailers->GrpcStatus() != nullptr) {
    addGrpcTags(span, *response_trailers);
  } else if (response_headers && response_headers->GrpcStatus() != nullptr) {
    addGrpcTags(span, *response_headers);
  }

  if (tracing_config.verbose()) {
    annotateVerbose(span, stream_info);
  }

  if (!stream_info.responseCode() || Http::CodeUtility::is5xx(stream_info.responseCode().value())) {
    span.setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  }
}

CustomTagPtr HttpTracerUtility::createCustomTag(const envoy::type::TracingCustomTag& tag) {
  switch (tag.type_case()) {
  case envoy::type::TracingCustomTag::kLiteral:
    return std::make_shared<Tracing::LiteralCustomTag>(tag.tag(), tag.literal());
  case envoy::type::TracingCustomTag::kEnvironment:
    return std::make_shared<Tracing::EnvironmentCustomTag>(tag.tag(), tag.environment());
  case envoy::type::TracingCustomTag::kRequestHeader:
    return std::make_shared<Tracing::RequestHeaderCustomTag>(tag.tag(), tag.request_header());
  case envoy::type::TracingCustomTag::kRequestMetadata:
    return std::make_shared<Tracing::RequestMetadataCustomTag>(tag.tag(), tag.request_metadata());
  case envoy::type::TracingCustomTag::kRouteMetadata:
    return std::make_shared<Tracing::RouteMetadataCustomTag>(tag.tag(), tag.route_metadata());
  case envoy::type::TracingCustomTag::TYPE_NOT_SET:
    return nullptr;
  }
  return nullptr;
}

HttpTracerImpl::HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr HttpTracerImpl::startSpan(const Config& config, Http::HeaderMap& request_headers,
                                  const StreamInfo::StreamInfo& stream_info,
                                  const Tracing::Decision tracing_decision) {
  std::string span_name = HttpTracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(std::string(request_headers.Host()->value().getStringView()));
  }

  SpanPtr active_span = driver_->startSpan(config, request_headers, span_name,
                                           stream_info.startTime(), tracing_decision);

  // Set tags related to the local environment
  if (active_span) {
    active_span->setTag(Tracing::Tags::get().NodeId, local_info_.nodeName());
    active_span->setTag(Tracing::Tags::get().Zone, local_info_.zoneName());
  }

  return active_span;
}

void GeneralCustomTag::apply(Span& span, const CustomTagContext& ctx) const {
  const absl::string_view& tag_value = value(ctx);
  if (!tag_value.empty()) {
    span.setTag(tag(), tag_value);
  }
}

EnvironmentCustomTag::EnvironmentCustomTag(
    const std::string& tag, const envoy::type::TracingCustomTag::Environment& environment)
    : GeneralCustomTag(tag), name_(environment.name()),
      default_value_(environment.default_value()) {
  const char* env = std::getenv(name_.data());
  final_value_ = env ? env : default_value_;
}

EnvironmentCustomTag::EnvironmentCustomTag(const std::string& tag, const std::string& name)
    : GeneralCustomTag(tag), name_(name) {
  const char* env = std::getenv(name_.data());
  final_value_ = env ? env : default_value_;
}

EnvironmentCustomTag::EnvironmentCustomTag(const std::string& tag, const std::string& name,
                                           const std::string& default_value)
    : GeneralCustomTag(tag), name_(name), default_value_(default_value) {
  const char* env = std::getenv(name_.data());
  final_value_ = env ? env : default_value_;
}

RequestHeaderCustomTag::RequestHeaderCustomTag(
    const std::string& tag, const envoy::type::TracingCustomTag::Header& request_header)
    : GeneralCustomTag(tag), name_(Http::LowerCaseString(request_header.name())),
      default_value_(request_header.default_value()) {}

RequestHeaderCustomTag::RequestHeaderCustomTag(const std::string& tag, const std::string& name)
    : GeneralCustomTag(tag), name_(Http::LowerCaseString(name)) {}

RequestHeaderCustomTag::RequestHeaderCustomTag(const std::string& tag, const std::string& name,
                                               const std::string& default_value)
    : GeneralCustomTag(tag), name_(Http::LowerCaseString(name)), default_value_(default_value) {}

absl::string_view RequestHeaderCustomTag::value(const CustomTagContext& ctx) const {
  if (!ctx.request_headers) {
    return default_value_;
  }
  const Http::HeaderEntry* entry = ctx.request_headers->get(name_);
  return entry ? entry->value().getStringView() : default_value_;
}

const std::string MetadataCustomTag::DefaultSeparator = ".";

MetadataCustomTag::MetadataCustomTag(const std::string& tag,
                                     const envoy::type::TracingCustomTag::Metadata& metadata)
    : GeneralCustomTag(tag), filter_namespace_(metadata.filter_namespace()),
      raw_path_(metadata.path()),
      separator_(metadata.path_separator().empty() ? DefaultSeparator : metadata.path_separator()),
      default_value_(metadata.default_value()), path_(absl::StrSplit(raw_path_, separator_)) {}

MetadataCustomTag::MetadataCustomTag(const std::string& tag, const std::string& filter_namespace,
                                     const std::string& raw_path)
    : GeneralCustomTag(tag), filter_namespace_(filter_namespace), raw_path_(raw_path),
      separator_(DefaultSeparator), path_(absl::StrSplit(raw_path_, separator_)) {}

MetadataCustomTag::MetadataCustomTag(const std::string& tag, const std::string& filter_namespace,
                                     const std::string& raw_path, const std::string& separator,
                                     const std::string& default_value)
    : GeneralCustomTag(tag), filter_namespace_(filter_namespace), raw_path_(raw_path),
      separator_(separator.empty() ? DefaultSeparator : separator), default_value_(default_value),
      path_(absl::StrSplit(raw_path_, separator_)) {}

void MetadataCustomTag::apply(Span& span, const CustomTagContext& ctx) const {
  const envoy::api::v2::core::Metadata* meta = metadata(ctx);
  if (!meta) {
    if (!default_value_.empty()) {
      span.setTag(tag(), default_value_);
    }
    return;
  }
  const ProtobufWkt::Value& value =
      Envoy::Config::Metadata::metadataValue(*meta, filter_namespace_, path_);
  switch (value.kind_case()) {
  case ProtobufWkt::Value::kBoolValue:
    span.setTag(tag(), value.bool_value() ? "true" : "false");
    return;
  case ProtobufWkt::Value::kNumberValue:
    span.setTag(tag(), fmt::format("{}", value.number_value()));
    return;
  case ProtobufWkt::Value::kListValue:
    span.setTag(tag(), MessageUtil::getJsonStringFromMessage(value.list_value()));
    return;
  case ProtobufWkt::Value::kStructValue:
    span.setTag(tag(), MessageUtil::getJsonStringFromMessage(value.struct_value()));
    return;
  case ProtobufWkt::Value::kStringValue: {
    if (!value.string_value().empty()) {
      span.setTag(tag(), value.string_value());
      return;
    }
    break;
  }
  default:
    break;
  }
  if (!default_value_.empty()) {
    span.setTag(tag(), default_value_);
  }
}

const envoy::api::v2::core::Metadata*
RequestMetadataCustomTag::metadata(const CustomTagContext& ctx) const {
  return &ctx.stream_info.dynamicMetadata();
}

const envoy::api::v2::core::Metadata*
RouteMetadataCustomTag::metadata(const CustomTagContext& ctx) const {
  const Router::RouteEntry* route_entry = ctx.stream_info.routeEntry();
  return route_entry ? &route_entry->metadata() : nullptr;
}

} // namespace Tracing
} // namespace Envoy
