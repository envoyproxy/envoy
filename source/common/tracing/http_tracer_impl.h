#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/common/platform.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/tracing/v2/custom_tag.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/metadata.h"
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
  const std::string ErrorReason = "error.reason";
  const std::string GrpcStatusCode = "grpc.status_code";
  const std::string GrpcMessage = "grpc.message";
  const std::string GuidXClientTraceId = "guid:x-client-trace-id";
  const std::string GuidXRequestId = "guid:x-request-id";
  const std::string HttpProtocol = "http.protocol";
  const std::string NodeId = "node_id";
  const std::string RequestSize = "request_size";
  const std::string ResponseFlags = "response_flags";
  const std::string ResponseSize = "response_size";
  const std::string RetryCount = "retry.count";
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
   * Adds information obtained from the downstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeDownstreamSpan(Span& span, const Http::HeaderMap* request_headers,
                                     const Http::HeaderMap* response_headers,
                                     const Http::HeaderMap* response_trailers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config);

  /**
   * Adds information obtained from the upstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeUpstreamSpan(Span& span, const Http::HeaderMap* response_headers,
                                   const Http::HeaderMap* response_trailers,
                                   const StreamInfo::StreamInfo& stream_info,
                                   const Config& tracing_config);

  /**
   * Create a custom tag according to the configuration.
   * @param tag a tracing custom tag configuration.
   */
  static CustomTagConstSharedPtr createCustomTag(const envoy::type::tracing::v2::CustomTag& tag);

private:
  static void setCommonTags(Span& span, const Http::HeaderMap* response_headers,
                            const Http::HeaderMap* response_trailers,
                            const StreamInfo::StreamInfo& stream_info,
                            const Config& tracing_config);

  static const std::string IngressOperation;
  static const std::string EgressOperation;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const CustomTagMap& customTags() const override { return custom_tags_; }
  bool verbose() const override { return false; }
  uint32_t maxPathTagLength() const override { return Tracing::DefaultMaxPathTagLength; }

private:
  const CustomTagMap custom_tags_{};
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

class GeneralCustomTag : public CustomTag {
public:
  explicit GeneralCustomTag(const std::string& tag) : tag_(tag) {}
  absl::string_view tag() const override { return tag_; }
  void apply(Span& span, const CustomTagContext& ctx) const override;

  virtual absl::string_view value(const CustomTagContext& ctx) const PURE;
  virtual std::string toString() const PURE;

protected:
  std::string tag_;
};

class LiteralCustomTag : public GeneralCustomTag {
public:
  LiteralCustomTag(const std::string& tag,
                   const envoy::type::tracing::v2::CustomTag::Literal& literal)
      : GeneralCustomTag(tag), value_(literal.value()) {}
  absl::string_view value(const CustomTagContext&) const override { return value_; }
  std::string toString() const override { return fmt::format("LITERAL|{}|{}", tag_, value_); }

private:
  std::string value_;
};

class EnvironmentCustomTag : public GeneralCustomTag {
public:
  EnvironmentCustomTag(const std::string& tag,
                       const envoy::type::tracing::v2::CustomTag::Environment& environment);
  absl::string_view value(const CustomTagContext&) const override { return final_value_; }
  std::string toString() const override {
    return fmt::format("ENVIRONMENT|{}|{}|{}|{}", tag_, name_, default_value_, final_value_);
  }

private:
  std::string name_;
  std::string default_value_;
  std::string final_value_;
};

class RequestHeaderCustomTag : public GeneralCustomTag {
public:
  RequestHeaderCustomTag(const std::string& tag,
                         const envoy::type::tracing::v2::CustomTag::Header& request_header);
  absl::string_view value(const CustomTagContext& ctx) const override;
  std::string toString() const override {
    return fmt::format("REQUEST_HEADER|{}|{}|{}", tag_, name_.get(), default_value_);
  }

private:
  Http::LowerCaseString name_;
  std::string default_value_;
};

class MetadataCustomTag : public GeneralCustomTag {
public:
  MetadataCustomTag(const std::string& tag,
                    const envoy::type::tracing::v2::CustomTag::Metadata& metadata);
  void apply(Span& span, const CustomTagContext& ctx) const override;
  absl::string_view value(const CustomTagContext&) const override { return default_value_; }
  const envoy::api::v2::core::Metadata* metadata(const CustomTagContext& ctx) const;
  std::string toString() const override {
    return fmt::format("METADATA|{}|{}|{}|{}|{}", kind_, tag_, metadata_key_.key_,
                       absl::StrJoin(metadata_key_.path_, "."), default_value_);
  }

protected:
  envoy::type::metadata::v2::MetadataKind::KindCase kind_;
  Envoy::Config::MetadataKey metadata_key_;
  std::string default_value_;
};

} // namespace Tracing
} // namespace Envoy
