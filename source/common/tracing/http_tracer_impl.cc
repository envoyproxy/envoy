#include "common/tracing/http_tracer_impl.h"

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/type/metadata/v3/metadata.pb.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/formatter/substitution_formatter.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"
#include "common/stream_info/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Tracing {

// TODO(perf): Avoid string creations/copies in this entire file.
static std::string buildResponseCode(const StreamInfo::StreamInfo& info) {
  return info.responseCode() ? std::to_string(info.responseCode().value()) : "0";
}

static absl::string_view valueOrDefault(const Http::HeaderEntry* header,
                                        const char* default_value) {
  return header ? header->value().getStringView() : default_value;
}

static std::string buildUrl(const Http::RequestHeaderMap& request_headers,
                            const uint32_t max_path_length) {
  if (!request_headers.Path()) {
    return "";
  }
  absl::string_view path(request_headers.EnvoyOriginalPath()
                             ? request_headers.getEnvoyOriginalPathValue()
                             : request_headers.getPathValue());

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return absl::StrCat(request_headers.getForwardedProtoValue(), "://",
                      request_headers.getHostValue(), path);
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
                                      const Http::RequestHeaderMap& request_headers) {
  // Exclude health check requests immediately.
  if (stream_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  Http::TraceStatus trace_status =
      stream_info.getRequestIDExtension()->getTraceStatus(request_headers);

  switch (trace_status) {
  case Http::TraceStatus::Client:
    return {Reason::ClientForced, true};
  case Http::TraceStatus::Forced:
    return {Reason::ServiceForced, true};
  case Http::TraceStatus::Sampled:
    return {Reason::Sampling, true};
  case Http::TraceStatus::NoTrace:
    return {Reason::NotTraceableRequestId, false};
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

static void addTagIfNotNull(Span& span, const std::string& tag, const Http::HeaderEntry* entry) {
  if (entry != nullptr) {
    span.setTag(tag, entry->value().getStringView());
  }
}

static void addGrpcRequestTags(Span& span, const Http::RequestHeaderMap& headers) {
  addTagIfNotNull(span, Tracing::Tags::get().GrpcPath, headers.Path());
  addTagIfNotNull(span, Tracing::Tags::get().GrpcAuthority, headers.Host());
  addTagIfNotNull(span, Tracing::Tags::get().GrpcContentType, headers.ContentType());
  addTagIfNotNull(span, Tracing::Tags::get().GrpcTimeout, headers.GrpcTimeout());
}

template <class T> static void addGrpcResponseTags(Span& span, const T& headers) {
  addTagIfNotNull(span, Tracing::Tags::get().GrpcStatusCode, headers.GrpcStatus());
  addTagIfNotNull(span, Tracing::Tags::get().GrpcMessage, headers.GrpcMessage());
  // Set error tag when status is not OK.
  absl::optional<Grpc::Status::GrpcStatus> grpc_status_code = Grpc::Common::getGrpcStatus(headers);
  if (grpc_status_code && grpc_status_code.value() != Grpc::Status::WellKnownGrpcStatus::Ok) {
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

void HttpTracerUtility::finalizeDownstreamSpan(Span& span,
                                               const Http::RequestHeaderMap* request_headers,
                                               const Http::ResponseHeaderMap* response_headers,
                                               const Http::ResponseTrailerMap* response_trailers,
                                               const StreamInfo::StreamInfo& stream_info,
                                               const Config& tracing_config) {
  // Pre response data.
  if (request_headers) {
    if (request_headers->RequestId()) {
      span.setTag(Tracing::Tags::get().GuidXRequestId, request_headers->getRequestIdValue());
    }
    span.setTag(Tracing::Tags::get().HttpUrl,
                buildUrl(*request_headers, tracing_config.maxPathTagLength()));
    span.setTag(Tracing::Tags::get().HttpMethod, request_headers->getMethodValue());
    span.setTag(Tracing::Tags::get().DownstreamCluster,
                valueOrDefault(request_headers->EnvoyDownstreamServiceCluster(), "-"));
    span.setTag(Tracing::Tags::get().UserAgent, valueOrDefault(request_headers->UserAgent(), "-"));
    span.setTag(
        Tracing::Tags::get().HttpProtocol,
        Formatter::SubstitutionFormatUtils::protocolToStringOrDefault(stream_info.protocol()));

    const auto& remote_address = stream_info.downstreamDirectRemoteAddress();

    if (remote_address->type() == Network::Address::Type::Ip) {
      const auto remote_ip = remote_address->ip();
      span.setTag(Tracing::Tags::get().PeerAddress, remote_ip->addressAsString());
    } else {
      span.setTag(Tracing::Tags::get().PeerAddress, remote_address->logicalName());
    }

    if (request_headers->ClientTraceId()) {
      span.setTag(Tracing::Tags::get().GuidXClientTraceId,
                  request_headers->getClientTraceIdValue());
    }

    if (Grpc::Common::isGrpcRequestHeaders(*request_headers)) {
      addGrpcRequestTags(span, *request_headers);
    }
  }
  CustomTagContext ctx{request_headers, stream_info};

  const CustomTagMap* custom_tag_map = tracing_config.customTags();
  if (custom_tag_map) {
    for (const auto& it : *custom_tag_map) {
      it.second->apply(span, ctx);
    }
  }
  span.setTag(Tracing::Tags::get().RequestSize, std::to_string(stream_info.bytesReceived()));
  span.setTag(Tracing::Tags::get().ResponseSize, std::to_string(stream_info.bytesSent()));

  setCommonTags(span, response_headers, response_trailers, stream_info, tracing_config);

  span.finishSpan();
}

void HttpTracerUtility::finalizeUpstreamSpan(Span& span,
                                             const Http::ResponseHeaderMap* response_headers,
                                             const Http::ResponseTrailerMap* response_trailers,
                                             const StreamInfo::StreamInfo& stream_info,
                                             const Config& tracing_config) {
  span.setTag(
      Tracing::Tags::get().HttpProtocol,
      Formatter::SubstitutionFormatUtils::protocolToStringOrDefault(stream_info.protocol()));

  if (stream_info.upstreamHost()) {
    span.setTag(Tracing::Tags::get().UpstreamAddress,
                stream_info.upstreamHost()->address()->asStringView());
  }

  setCommonTags(span, response_headers, response_trailers, stream_info, tracing_config);

  span.finishSpan();
}

void HttpTracerUtility::setCommonTags(Span& span, const Http::ResponseHeaderMap* response_headers,
                                      const Http::ResponseTrailerMap* response_trailers,
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
    addGrpcResponseTags(span, *response_trailers);
  } else if (response_headers && response_headers->GrpcStatus() != nullptr) {
    addGrpcResponseTags(span, *response_headers);
  }

  if (tracing_config.verbose()) {
    annotateVerbose(span, stream_info);
  }

  if (!stream_info.responseCode() || Http::CodeUtility::is5xx(stream_info.responseCode().value())) {
    span.setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  }
}

CustomTagConstSharedPtr
HttpTracerUtility::createCustomTag(const envoy::type::tracing::v3::CustomTag& tag) {
  switch (tag.type_case()) {
  case envoy::type::tracing::v3::CustomTag::TypeCase::kLiteral:
    return std::make_shared<const Tracing::LiteralCustomTag>(tag.tag(), tag.literal());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kEnvironment:
    return std::make_shared<const Tracing::EnvironmentCustomTag>(tag.tag(), tag.environment());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kRequestHeader:
    return std::make_shared<const Tracing::RequestHeaderCustomTag>(tag.tag(), tag.request_header());
  case envoy::type::tracing::v3::CustomTag::TypeCase::kMetadata:
    return std::make_shared<const Tracing::MetadataCustomTag>(tag.tag(), tag.metadata());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

HttpTracerImpl::HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr HttpTracerImpl::startSpan(const Config& config, Http::RequestHeaderMap& request_headers,
                                  const StreamInfo::StreamInfo& stream_info,
                                  const Tracing::Decision tracing_decision) {
  std::string span_name = HttpTracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(std::string(request_headers.getHostValue()));
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

void CustomTagBase::apply(Span& span, const CustomTagContext& ctx) const {
  absl::string_view tag_value = value(ctx);
  if (!tag_value.empty()) {
    span.setTag(tag(), tag_value);
  }
}

EnvironmentCustomTag::EnvironmentCustomTag(
    const std::string& tag, const envoy::type::tracing::v3::CustomTag::Environment& environment)
    : CustomTagBase(tag), name_(environment.name()), default_value_(environment.default_value()) {
  const char* env = std::getenv(name_.data());
  final_value_ = env ? env : default_value_;
}

RequestHeaderCustomTag::RequestHeaderCustomTag(
    const std::string& tag, const envoy::type::tracing::v3::CustomTag::Header& request_header)
    : CustomTagBase(tag), name_(Http::LowerCaseString(request_header.name())),
      default_value_(request_header.default_value()) {}

absl::string_view RequestHeaderCustomTag::value(const CustomTagContext& ctx) const {
  if (!ctx.request_headers) {
    return default_value_;
  }
  // TODO(https://github.com/envoyproxy/envoy/issues/13454): Potentially populate all header values.
  const auto entry = ctx.request_headers->get(name_);
  return !entry.empty() ? entry[0]->value().getStringView() : default_value_;
}

MetadataCustomTag::MetadataCustomTag(const std::string& tag,
                                     const envoy::type::tracing::v3::CustomTag::Metadata& metadata)
    : CustomTagBase(tag), kind_(metadata.kind().kind_case()),
      metadata_key_(metadata.metadata_key()), default_value_(metadata.default_value()) {}

void MetadataCustomTag::apply(Span& span, const CustomTagContext& ctx) const {
  const envoy::config::core::v3::Metadata* meta = metadata(ctx);
  if (!meta) {
    if (!default_value_.empty()) {
      span.setTag(tag(), default_value_);
    }
    return;
  }
  const ProtobufWkt::Value& value = Envoy::Config::Metadata::metadataValue(meta, metadata_key_);
  switch (value.kind_case()) {
  case ProtobufWkt::Value::kBoolValue:
    span.setTag(tag(), value.bool_value() ? "true" : "false");
    return;
  case ProtobufWkt::Value::kNumberValue:
    span.setTag(tag(), absl::StrCat("", value.number_value()));
    return;
  case ProtobufWkt::Value::kStringValue:
    span.setTag(tag(), value.string_value());
    return;
  case ProtobufWkt::Value::kListValue:
    span.setTag(tag(), MessageUtil::getJsonStringFromMessage(value.list_value()));
    return;
  case ProtobufWkt::Value::kStructValue:
    span.setTag(tag(), MessageUtil::getJsonStringFromMessage(value.struct_value()));
    return;
  default:
    break;
  }
  if (!default_value_.empty()) {
    span.setTag(tag(), default_value_);
  }
}

const envoy::config::core::v3::Metadata*
MetadataCustomTag::metadata(const CustomTagContext& ctx) const {
  const StreamInfo::StreamInfo& info = ctx.stream_info;
  switch (kind_) {
  case envoy::type::metadata::v3::MetadataKind::KindCase::kRequest:
    return &info.dynamicMetadata();
  case envoy::type::metadata::v3::MetadataKind::KindCase::kRoute: {
    const Router::RouteEntry* route_entry = info.routeEntry();
    return route_entry ? &route_entry->metadata() : nullptr;
  }
  case envoy::type::metadata::v3::MetadataKind::KindCase::kCluster: {
    const auto& hostPtr = info.upstreamHost();
    return hostPtr ? &hostPtr->cluster().metadata() : nullptr;
  }
  case envoy::type::metadata::v3::MetadataKind::KindCase::kHost: {
    const auto& hostPtr = info.upstreamHost();
    return hostPtr ? hostPtr->metadata().get() : nullptr;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Tracing
} // namespace Envoy
