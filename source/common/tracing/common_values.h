#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Tracing {

/**
 * Tracing tag names.
 */
class TracingTagValues {
public:
  // OpenTracing standard tag names.
  const std::string Component = "component";
  const std::string DbInstance = "db.instance";
  const std::string DbStatement = "db.statement";
  const std::string DbUser = "db.user";
  const std::string DbType = "db.type";
  const std::string Error = "error";
  const std::string HttpMethod = "http.method";
  const std::string HttpStatusCode = "http.status_code";
  const std::string HttpUrl = "http.url";
  const std::string MessageBusDestination = "message_bus.destination";
  // Downstream remote address for the downstream span and upstream remote address for the upstream
  // span.
  const std::string PeerAddress = "peer.address";
  const std::string PeerHostname = "peer.hostname";
  const std::string PeerIpv4 = "peer.ipv4";
  const std::string PeerIpv6 = "peer.ipv6";
  const std::string PeerPort = "peer.port";
  const std::string PeerService = "peer.service";
  const std::string SpanKind = "span.kind";

  // Non-standard tag names.
  const std::string DownstreamCluster = "downstream_cluster";
  const std::string ErrorReason = "error.reason";
  const std::string GrpcAuthority = "grpc.authority";
  const std::string GrpcContentType = "grpc.content_type";
  const std::string GrpcMessage = "grpc.message";
  const std::string GrpcPath = "grpc.path";
  const std::string GrpcStatusCode = "grpc.status_code";
  const std::string GrpcTimeout = "grpc.timeout";
  const std::string GuidXClientTraceId = "guid:x-client-trace-id";
  const std::string GuidXRequestId = "guid:x-request-id";
  const std::string HttpProtocol = "http.protocol";
  const std::string NodeId = "node_id";
  const std::string RequestSize = "request_size";
  const std::string ResponseFlags = "response_flags";
  const std::string ResponseSize = "response_size";
  const std::string RetryCount = "retry.count";
  const std::string Status = "status";
  const std::string UpstreamAddress = "upstream_address";
  const std::string UpstreamCluster = "upstream_cluster";
  const std::string UpstreamClusterName = "upstream_cluster.name";
  const std::string UserAgent = "user_agent";
  const std::string Zone = "zone";

  // Tag values.
  const std::string Canceled = "canceled";
  const std::string Proxy = "proxy";
  const std::string True = "true";

  // OpenTelemetry HTTP semantic convention attributes
  // Follow https://github.com/open-telemetry/semantic-conventions/blob/main/docs/http/http-spans.md
  // Required, e.g. GET; POST; HEAD
  const std::string HttpRequestMethod = "http.request.method";
  // Conditionally Required If and only if one was received/sent.
  const std::string HttpResponseStatusCode = "http.response.status_code";
  // Conditionally Required If request has ended with an error.
  // e.g. timeout; java.net.UnknownHostException; server_certificate_invalid;
  const std::string ErrorType = "error.type";

  // Required, e.g.  https://www.foo.bar/search?q=OpenTelemetry#SemConv;
  // same as http.url
  const std::string UrlFull = "url.full";
  // Required, e.g. /search
  const std::string UrlPath = "url.path";
  // Required, http; https
  // The scheme of the original client request, 
  // if known (e.g. from Forwarded#proto, X-Forwarded-Proto,
  // or a similar header). Otherwise, the scheme of the immediate peer request.
  const std::string UrlScheme = "url.scheme";
  
  // Recommended if and only if request was retried.
  const std::string HttpRequestResendCount = "http.request.resend_count";
  // Opt-In, The total size of the request in bytes. 
  const std::string HttpRequestSize = "http.request.size";
  const std::string HttpResponseSize = "http.response.size";

  // Recommended, e.g. 83.164.160.102
  // The IP address of the original client behind all proxies, 
  // if known (e.g. from Forwarded#for, X-Forwarded-For, or a similar header). 
  // Otherwise, the immediate client peer address.
  const std::string ClientAddress = "client.address";
  const std::string ClientPort = "client.port";

  // Required, e.g. example.com; 10.1.2.80;
  // when populating server.address and server.port attributes and SHOULD determine them by using the first of the following that applies:
  //  The original host which may be passed by the reverse proxy in the Forwarded#host, X-Forwarded-Host, or a similar header.
  //  The :authority pseudo-header in case of HTTP/2 or HTTP/3
  //  The Host header.
  const std::string ServerAddress = "server.address";
  // Required, e.g. 80; 8080; 443
  const std::string ServerPort = "server.port";

  // Conditionally Required, e.g. http; spdy
  // The value SHOULD be normalized to lowercase.
  const std::string NetworkProtocolName = "network.protocol.name";
  // Recommended
  // downstream span: downstream remote address
  // upstream span: upstream remote address
  const std::string NetworkPeerAddress = "network.peer.address";
  const std::string NetworkPeerPort = "network.peer.port";
  const std::string NetworkProtocolVersion = "network.protocol.version";

  // Opt-In
  const std::string UserAgentOriginal="user_agent.original";

  std::string httpRequestMethod(bool use_semantic_conventions) const {
    return use_semantic_conventions ? HttpRequestMethod : HttpMethod;
  }

  std::string httpStatusCode(bool use_semantic_conventions) const {
    return use_semantic_conventions ? HttpResponseStatusCode : HttpStatusCode;
  }

  std::string urlFull(bool use_semantic_conventions) const {
    return use_semantic_conventions ? UrlFull : HttpUrl;
  }

  std::string urlScheme(bool use_semantic_conventions) const {
    return use_semantic_conventions ? UrlScheme : HttpProtocol;
  }

  std::string requestSize(bool use_semantic_conventions) const {
    return use_semantic_conventions ? HttpRequestSize : RequestSize;
  }

  std::string responseSize(bool use_semantic_conventions) const {
    return use_semantic_conventions ? HttpResponseSize : ResponseSize;
  }

  std::string userAgent(bool use_semantic_conventions) const {
    return use_semantic_conventions ? UserAgentOriginal : UserAgent;
  }

  std::string peerAddress(bool use_semantic_conventions) const {
    return use_semantic_conventions ? NetworkPeerAddress : PeerAddress;
  }

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
