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
