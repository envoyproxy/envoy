#pragma once

#include "envoy/tracing/trace_tag.h"

#include "source/common/singleton/const_singleton.h"
namespace Envoy {
namespace Tracing {

/**
 * OpenTelemetry standard HTTP/Network semantic convention tag names.
 * Follows: https://opentelemetry.io/docs/specs/semconv/http/http-spans/
 */
class SemanticConventionTagValues {
public:
  const std::string HttpRequestMethod = "http.request.method";
  const std::string HttpResponseStatusCode = "http.response.status_code";
  const std::string ErrorType = "error.type";
  const std::string UrlFull = "url.full";
  const std::string UrlPath = "url.path";
  const std::string UrlScheme = "url.scheme";
  const std::string HttpRequestResendCount = "http.request.resend_count";
  const std::string HttpRequestSize = "http.request.body.size";
  const std::string HttpResponseSize = "http.response.body.size";
  const std::string ClientAddress = "client.address";
  const std::string ClientPort = "client.port";
  const std::string ServerAddress = "server.address";
  const std::string ServerPort = "server.port";
  const std::string NetworkProtocolName = "network.protocol.name";
  const std::string NetworkPeerAddress = "network.peer.address";
  const std::string NetworkPeerPort = "network.peer.port";
  const std::string NetworkProtocolVersion = "network.protocol.version";
  const std::string UserAgentOriginal = "user_agent.original";
};

using SemanticConventionTags = ConstSingleton<SemanticConventionTagValues>;

/**
 * Tracing tag names.
 */
class TracingTagValues {
public:
  // OpenTracing standard tag names.
  const Tag Component{"component", TagName::Component};
  const Tag DbInstance{"db.instance", TagName::DbInstance};
  const Tag DbStatement{"db.statement", TagName::DbStatement};
  const Tag DbUser{"db.user", TagName::DbUser};
  const Tag DbType{"db.type", TagName::DbType};
  const Tag Error{"error", TagName::Error};
  const Tag HttpMethod{"http.method", TagName::HttpMethod,
                       SemanticConventionTags::get().HttpRequestMethod};
  const Tag HttpStatusCode{"http.status_code", TagName::HttpStatusCode,
                           SemanticConventionTags::get().HttpResponseStatusCode};
  const Tag HttpUrl{"http.url", TagName::HttpUrl, SemanticConventionTags::get().UrlFull};
  const Tag MessageBusDestination{"message_bus.destination", TagName::MessageBusDestination};
  // Downstream remote address for the downstream span and upstream remote address for the upstream
  // span.
  const Tag PeerAddress{"peer.address", TagName::PeerAddress,
                        SemanticConventionTags::get().NetworkPeerAddress};
  const Tag PeerHostname{"peer.hostname", TagName::PeerHostname};
  const Tag PeerIpv4{"peer.ipv4", TagName::PeerIpv4};
  const Tag PeerIpv6{"peer.ipv6", TagName::PeerIpv6};
  const Tag PeerPort{"peer.port", TagName::PeerPort};
  const Tag PeerService{"peer.service", TagName::PeerService};
  const Tag SpanKind{"span.kind", TagName::SpanKind};

  // Non-standard tag names.
  const Tag DownstreamCluster{"downstream_cluster", TagName::DownstreamCluster};
  const Tag ErrorReason{"error.reason", TagName::ErrorReason};
  const Tag GrpcAuthority{"grpc.authority", TagName::GrpcAuthority};
  const Tag GrpcContentType{"grpc.content_type", TagName::GrpcContentType};
  const Tag GrpcMessage{"grpc.message", TagName::GrpcMessage};
  const Tag GrpcPath{"grpc.path", TagName::GrpcPath};
  const Tag GrpcStatusCode{"grpc.status_code", TagName::GrpcStatusCode};
  const Tag GrpcTimeout{"grpc.timeout", TagName::GrpcTimeout};
  const Tag GuidXClientTraceId{"guid:x-client-trace-id", TagName::GuidXClientTraceId};
  const Tag GuidXRequestId{"guid:x-request-id", TagName::GuidXRequestId};
  const Tag HttpProtocol{"http.protocol", TagName::HttpProtocol};
  const Tag NodeId{"node_id", TagName::NodeId};
  const Tag RequestSize{"request_size", TagName::RequestSize,
                        SemanticConventionTags::get().HttpRequestSize};
  const Tag ResponseFlags{"response_flags", TagName::ResponseFlags};
  const Tag ResponseSize{"response_size", TagName::ResponseSize,
                         SemanticConventionTags::get().HttpResponseSize};
  const Tag RetryCount{"retry.count", TagName::RetryCount,
                       SemanticConventionTags::get().HttpRequestResendCount};
  const Tag Status{"status", TagName::Status};
  const Tag UpstreamAddress{"upstream_address", TagName::UpstreamAddress};
  const Tag UpstreamCluster{"upstream_cluster", TagName::UpstreamCluster};
  const Tag UpstreamClusterName{"upstream_cluster.name", TagName::UpstreamClusterName};
  const Tag UserAgent{"user_agent", TagName::UserAgent,
                      SemanticConventionTags::get().UserAgentOriginal};
  const Tag Zone{"zone", TagName::Zone};

  // Tag values.
  const std::string Canceled = "canceled";
  const std::string Proxy = "proxy";
  const std::string True = "true";
};

using Tags = ConstSingleton<TracingTagValues>;

class TracingLogValues {
public:
  // OpenTracing standard key names.
  const std::string EventKey = "event";

  // Event names
  const std::string LastDownstreamRxByteReceived = "last_downstream_rx_byte_received";
  const std::string FirstUpstreamTxByteSent = "first_upstream_tx_byte_sent";
  const std::string LastUpstreamTxByteSent = "last_upstream_tx_byte_sent";
  const std::string FirstUpstreamRxByteReceived = "first_upstream_rx_byte_received";
  const std::string LastUpstreamRxByteReceived = "last_upstream_rx_byte_received";
  const std::string FirstDownstreamTxByteSent = "first_downstream_tx_byte_sent";
  const std::string LastDownstreamTxByteSent = "last_downstream_tx_byte_sent";
};

using Logs = ConstSingleton<TracingLogValues>;

} // namespace Tracing
} // namespace Envoy
