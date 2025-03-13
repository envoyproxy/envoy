#include "source/common/tracing/http_tracer_impl.h"

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"
#include "envoy/tracing/tracer.h"
#include "envoy/type/metadata/v3/metadata.pb.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/utility.h"

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
  // Set error tag when Grpc status code represents an upstream error. See
  // https://github.com/envoyproxy/envoy/issues/18877.
  absl::optional<Grpc::Status::GrpcStatus> grpc_status_code = Grpc::Common::getGrpcStatus(headers);
  if (grpc_status_code.has_value()) {
    const auto& status = grpc_status_code.value();
    if (status != Grpc::Status::WellKnownGrpcStatus::InvalidCode) {
      switch (status) {
      // Each case below is considered to be a client side error, therefore should not be
      // tagged as an upstream error. See https://grpc.github.io/grpc/core/md_doc_statuscodes.html
      // for more details about how each Grpc status code is defined and whether it is an
      // upstream error or a client error.
      case Grpc::Status::WellKnownGrpcStatus::Ok:
      case Grpc::Status::WellKnownGrpcStatus::Canceled:
      case Grpc::Status::WellKnownGrpcStatus::InvalidArgument:
      case Grpc::Status::WellKnownGrpcStatus::NotFound:
      case Grpc::Status::WellKnownGrpcStatus::AlreadyExists:
      case Grpc::Status::WellKnownGrpcStatus::PermissionDenied:
      case Grpc::Status::WellKnownGrpcStatus::FailedPrecondition:
      case Grpc::Status::WellKnownGrpcStatus::Aborted:
      case Grpc::Status::WellKnownGrpcStatus::OutOfRange:
      case Grpc::Status::WellKnownGrpcStatus::Unauthenticated:
        break;
      case Grpc::Status::WellKnownGrpcStatus::Unknown:
      case Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded:
      case Grpc::Status::WellKnownGrpcStatus::Unimplemented:
      case Grpc::Status::WellKnownGrpcStatus::ResourceExhausted:
      case Grpc::Status::WellKnownGrpcStatus::Internal:
      case Grpc::Status::WellKnownGrpcStatus::Unavailable:
      case Grpc::Status::WellKnownGrpcStatus::DataLoss:
        span.setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
        break;
      }
    }
  }
}

static void annotateVerbose(Span& span, const StreamInfo::StreamInfo& stream_info) {
  const auto start_time = stream_info.startTime();
  StreamInfo::TimingUtility timing(stream_info);
  if (timing.lastDownstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastDownstreamRxByteReceived()),
             Tracing::Logs::get().LastDownstreamRxByteReceived);
  }
  if (timing.firstUpstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstUpstreamTxByteSent()),
             Tracing::Logs::get().FirstUpstreamTxByteSent);
  }
  if (timing.lastUpstreamTxByteSent()) {
    span.log(start_time +
                 std::chrono::duration_cast<SystemTime::duration>(*timing.lastUpstreamTxByteSent()),
             Tracing::Logs::get().LastUpstreamTxByteSent);
  }
  if (timing.firstUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstUpstreamRxByteReceived()),
             Tracing::Logs::get().FirstUpstreamRxByteReceived);
  }
  if (timing.lastUpstreamRxByteReceived()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastUpstreamRxByteReceived()),
             Tracing::Logs::get().LastUpstreamRxByteReceived);
  }
  if (timing.firstDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.firstDownstreamTxByteSent()),
             Tracing::Logs::get().FirstDownstreamTxByteSent);
  }
  if (timing.lastDownstreamTxByteSent()) {
    span.log(start_time + std::chrono::duration_cast<SystemTime::duration>(
                              *timing.lastDownstreamTxByteSent()),
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
    span.setTag(
        Tracing::Tags::get().HttpUrl,
        Http::Utility::buildOriginalUri(*request_headers, tracing_config.maxPathTagLength()));
    span.setTag(Tracing::Tags::get().HttpMethod, request_headers->getMethodValue());
    span.setTag(Tracing::Tags::get().DownstreamCluster,
                valueOrDefault(request_headers->EnvoyDownstreamServiceCluster(), "-"));
    span.setTag(Tracing::Tags::get().UserAgent, valueOrDefault(request_headers->UserAgent(), "-"));
    span.setTag(
        Tracing::Tags::get().HttpProtocol,
        Formatter::SubstitutionFormatUtils::protocolToStringOrDefault(stream_info.protocol()));

    const auto& remote_address = stream_info.downstreamAddressProvider().directRemoteAddress();

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

  span.setTag(Tracing::Tags::get().RequestSize, std::to_string(stream_info.bytesReceived()));
  span.setTag(Tracing::Tags::get().ResponseSize, std::to_string(stream_info.bytesSent()));

  setCommonTags(span, stream_info, tracing_config);
  onUpstreamResponseHeaders(span, response_headers);
  onUpstreamResponseTrailers(span, response_trailers);

  span.finishSpan();
}

void HttpTracerUtility::finalizeUpstreamSpan(Span& span, const StreamInfo::StreamInfo& stream_info,
                                             const Config& tracing_config) {
  span.setTag(
      Tracing::Tags::get().HttpProtocol,
      Formatter::SubstitutionFormatUtils::protocolToStringOrDefault(stream_info.protocol()));

  if (stream_info.upstreamInfo() && stream_info.upstreamInfo()->upstreamHost()) {
    auto upstream_address = stream_info.upstreamInfo()->upstreamHost()->address();
    // TODO(wbpcode): separated `upstream_address` may be meaningful to the downstream span.
    // But for the upstream span, `peer.address` should be used.
    span.setTag(Tracing::Tags::get().UpstreamAddress, upstream_address->asStringView());
    // TODO(wbpcode): may be set this tag in the setCommonTags.
    span.setTag(Tracing::Tags::get().PeerAddress, upstream_address->asStringView());
  }

  setCommonTags(span, stream_info, tracing_config);

  span.finishSpan();
}

void HttpTracerUtility::onUpstreamResponseHeaders(Span& span,
                                                  const Http::ResponseHeaderMap* response_headers) {
  if (response_headers && response_headers->GrpcStatus() != nullptr) {
    addGrpcResponseTags(span, *response_headers);
  }
}

void HttpTracerUtility::onUpstreamResponseTrailers(
    Span& span, const Http::ResponseTrailerMap* response_trailers) {
  if (response_trailers && response_trailers->GrpcStatus() != nullptr) {
    addGrpcResponseTags(span, *response_trailers);
  }
}

void HttpTracerUtility::setCommonTags(Span& span, const StreamInfo::StreamInfo& stream_info,
                                      const Config& tracing_config) {

  span.setTag(Tracing::Tags::get().Component, Tracing::Tags::get().Proxy);

  // Cluster info.
  if (auto cluster_info = stream_info.upstreamClusterInfo();
      cluster_info.has_value() && cluster_info.value() != nullptr) {
    span.setTag(Tracing::Tags::get().UpstreamCluster, cluster_info.value()->name());
    span.setTag(Tracing::Tags::get().UpstreamClusterName,
                cluster_info.value()->observabilityName());
  }

  // Post response data.
  span.setTag(Tracing::Tags::get().HttpStatusCode, buildResponseCode(stream_info));
  span.setTag(Tracing::Tags::get().ResponseFlags,
              StreamInfo::ResponseFlagUtils::toShortString(stream_info));

  if (tracing_config.verbose()) {
    annotateVerbose(span, stream_info);
  }

  if (!stream_info.responseCode() || Http::CodeUtility::is5xx(stream_info.responseCode().value())) {
    span.setTag(Tracing::Tags::get().Error, Tracing::Tags::get().True);
  }

  ReadOnlyHttpTraceContext trace_context{stream_info.getRequestHeaders() != nullptr
                                             ? *stream_info.getRequestHeaders()
                                             : *Http::StaticEmptyHeaders::get().request_headers};
  CustomTagContext ctx{trace_context, stream_info};
  if (const CustomTagMap* custom_tag_map = tracing_config.customTags(); custom_tag_map) {
    for (const auto& it : *custom_tag_map) {
      it.second->applySpan(span, ctx);
    }
  }
}

} // namespace Tracing
} // namespace Envoy
