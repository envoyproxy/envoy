#pragma once

#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

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
  const std::string PeerAddress = "peer.address";
  const std::string PeerHostname = "peer.hostname";
  const std::string PeerIpv4 = "peer.ipv4";
  const std::string PeerIpv6 = "peer.ipv6";
  const std::string PeerPort = "peer.port";
  const std::string PeerService = "peer.service";
  const std::string SpanKind = "span.kind";

  // Non-standard tag names.
  const std::string DownstreamCluster = "downstream_cluster";
  const std::string GrpcStatusCode = "grpc.status_code";
  const std::string GrpcMessage = "grpc.message";
  const std::string GuidXClientTraceId = "guid:x-client-trace-id";
  const std::string GuidXRequestId = "guid:x-request-id";
  const std::string HttpProtocol = "http.protocol";
  const std::string NodeId = "node_id";
  const std::string RequestSize = "request_size";
  const std::string ResponseFlags = "response_flags";
  const std::string ResponseSize = "response_size";
  const std::string Status = "status";
  const std::string UpstreamCluster = "upstream_cluster";
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

class HttpTracerUtility {
public:
  /**
   * Get string representation of the operation.
   * @param operation name to convert.
   * @return string representation of the operation.
   */
  static const std::string& toString(OperationName operation_name);

  /**
   * Request might be traceable if x-request-id is traceable uuid or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision isTracing(const StreamInfo::StreamInfo& stream_info,
                            const Http::HeaderMap& request_headers);

  /**
   * 1) Fill in span tags based on the response headers.
   * 2) Finish active span.
   */
  static void finalizeSpan(Span& span, const Http::HeaderMap* request_headers,
                           const Http::HeaderMap* response_headers,
                           const Http::HeaderMap* response_trailers,
                           const StreamInfo::StreamInfo& stream_info, const Config& tracing_config);

  static const std::string IngressOperation;
  static const std::string EgressOperation;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const std::vector<Http::LowerCaseString>& requestHeadersForTags() const override {
    return request_headers_for_tags_;
  }
  bool verbose() const override { return false; }

private:
  const std::vector<Http::LowerCaseString> request_headers_for_tags_{};
};

using EgressConfig = ConstSingleton<EgressConfigImpl>;

class NullSpan : public Span {
public:
  static NullSpan& instance() {
    static NullSpan* instance = new NullSpan();
    return *instance;
  }

  // Tracing::Span
  void setOperation(absl::string_view) override {}
  void setTag(absl::string_view, absl::string_view) override {}
  void log(SystemTime, const std::string&) override {}
  void finishSpan() override {}
  void injectContext(Http::HeaderMap&) override {}
  SpanPtr spawnChild(const Config&, const std::string&, SystemTime) override {
    return SpanPtr{new NullSpan()};
  }
  void setSampled(bool) override {}
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  SpanPtr startSpan(const Config&, Http::HeaderMap&, const StreamInfo::StreamInfo&,
                    const Tracing::Decision) override {
    return SpanPtr{new NullSpan()};
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const StreamInfo::StreamInfo& stream_info,
                    const Tracing::Decision tracing_decision) override;

private:
  DriverPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace Tracing
} // namespace Envoy
