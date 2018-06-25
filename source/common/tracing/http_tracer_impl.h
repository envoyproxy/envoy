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
  const std::string COMPONENT = "component";
  const std::string DB_INSTANCE = "db.instance";
  const std::string DB_STATEMENT = "db.statement";
  const std::string DB_USER = "db.user";
  const std::string DB_TYPE = "db.type";
  const std::string ERROR = "error";
  const std::string HTTP_METHOD = "http.method";
  const std::string HTTP_STATUS_CODE = "http.status_code";
  const std::string HTTP_URL = "http.url";
  const std::string MESSAGE_BUS_DESTINATION = "message_bus.destination";
  const std::string PEER_ADDRESS = "peer.address";
  const std::string PEER_HOSTNAME = "peer.hostname";
  const std::string PEER_IPV4 = "peer.ipv4";
  const std::string PEER_IPV6 = "peer.ipv6";
  const std::string PEER_PORT = "peer.port";
  const std::string PEER_SERVICE = "peer.service";
  const std::string SPAN_KIND = "span.kind";

  // Non-standard tag names.
  const std::string DOWNSTREAM_CLUSTER = "downstream_cluster";
  const std::string GRPC_STATUS_CODE = "grpc.status_code";
  const std::string GUID_X_CLIENT_TRACE_ID = "guid:x-client-trace-id";
  const std::string GUID_X_REQUEST_ID = "guid:x-request-id";
  const std::string HTTP_PROTOCOL = "http.protocol";
  const std::string NODE_ID = "node_id";
  const std::string REQUEST_SIZE = "request_size";
  const std::string RESPONSE_FLAGS = "response_flags";
  const std::string RESPONSE_SIZE = "response_size";
  const std::string STATUS = "status";
  const std::string UPSTREAM_CLUSTER = "upstream_cluster";
  const std::string USER_AGENT = "user_agent";
  const std::string ZONE = "zone";

  // Tag values.
  const std::string CANCELED = "canceled";
  const std::string PROXY = "proxy";
  const std::string TRUE = "true";
};

typedef ConstSingleton<TracingTagValues> Tags;

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
  static Decision isTracing(const RequestInfo::RequestInfo& request_info,
                            const Http::HeaderMap& request_headers);

  /**
   * 1) Fill in span tags based on the response headers.
   * 2) Finish active span.
   */
  static void finalizeSpan(Span& span, const Http::HeaderMap* request_headers,
                           const RequestInfo::RequestInfo& request_info,
                           const Config& tracing_config);

  static const std::string INGRESS_OPERATION;
  static const std::string EGRESS_OPERATION;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const std::vector<Http::LowerCaseString>& requestHeadersForTags() const override {
    return request_headers_for_tags_;
  }

private:
  const std::vector<Http::LowerCaseString> request_headers_for_tags_;
};

typedef ConstSingleton<EgressConfigImpl> EgressConfig;

class NullSpan : public Span {
public:
  static NullSpan& instance() {
    static NullSpan* instance = new NullSpan();
    return *instance;
  }

  // Tracing::Span
  void setOperation(const std::string&) override {}
  void setTag(const std::string&, const std::string&) override {}
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
  SpanPtr startSpan(const Config&, Http::HeaderMap&, const RequestInfo::RequestInfo&,
                    const Tracing::Decision) override {
    return SpanPtr{new NullSpan()};
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const RequestInfo::RequestInfo& request_info,
                    const Tracing::Decision tracing_decision) override;

private:
  DriverPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace Tracing
} // namespace Envoy
