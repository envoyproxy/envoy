#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_config.h"

namespace Envoy {
namespace Tracing {

class Span;
using SpanPtr = std::unique_ptr<Span>;

/**
 * The upstream service type.
 */
enum class ServiceType {
  // Service type is unknown.
  Unknown,
  // Service is treated as HTTP.
  Http,
  // Service is treated as GoogleGrpc.
  GoogleGrpc,
  // Service is treated as EnvoyGrpc.
  EnvoyGrpc
};

/**
 * Contains upstream context information essential for the injectContext process.
 *
 * @param host Optional reference to the upstream host description.
 * @param cluster Optional reference to the upstream cluster information.
 * @param service_type The type of service the upstream context relates to.
 * @param async_client_span Indicates if the injectContext originates from an asynchronous
 * client.
 */
struct UpstreamContext {
  UpstreamContext(const Upstream::HostDescription* host = nullptr,
                  const Upstream::ClusterInfo* cluster = nullptr,
                  const ServiceType service_type = ServiceType::Unknown,
                  const bool async_client_span = false)
      : host_(makeOptRefFromPtr(host)), cluster_(makeOptRefFromPtr(cluster)),
        service_type_(service_type), async_client_span_(async_client_span) {}

  OptRef<const Upstream::HostDescription> host_;
  OptRef<const Upstream::ClusterInfo> cluster_;
  const ServiceType service_type_;

  // TODO(botengyao): further distinction for the shared upstream code path can be
  // added if needed. Setting this flag to true only means it is called from async
  // client at current stage.
  const bool async_client_span_;
};

/**
 * Basic abstraction for span.
 */
class Span {
public:
  virtual ~Span() = default;

  /**
   * Set the operation name.
   * @param operation the operation name
   */
  virtual void setOperation(absl::string_view operation) PURE;

  /**
   * Attach metadata to a Span, to be handled in an implementation-dependent fashion.
   * @param name the name of the tag
   * @param value the value to associate with the tag
   */
  virtual void setTag(absl::string_view name, absl::string_view value) PURE;

  /**
   * Record an event associated with a span, to be handled in an implementation-dependent fashion.
   * @param timestamp the time of the event.
   * @param event the name of the event.
   */
  virtual void log(SystemTime timestamp, const std::string& event) PURE;

  /**
   * Capture the final duration for this Span and carry out any work necessary to complete it.
   * Once this method is called, the Span may be safely discarded.
   */
  virtual void finishSpan() PURE;

  /**
   * Mutate the provided headers with the context necessary to propagate this
   * (implementation-specific) trace.
   * @param request_headers the headers to which propagation context will be added
   * @param upstream upstream context info
   */
  virtual void injectContext(TraceContext& trace_conext, const UpstreamContext& upstream) PURE;

  /**
   * Create and start a child Span, with this Span as its parent in the trace.
   * @param config the tracing configuration
   * @param name operation name captured by the spawned child
   * @param start_time initial start time for the operation captured by the child
   */
  virtual SpanPtr spawnChild(const Config& config, const std::string& name,
                             SystemTime start_time) PURE;

  /**
   * This method overrides any previous sampling decision associated with the trace instance.
   * If the sampled parameter is false, this span and any subsequent child spans
   * are not reported to the tracing system.
   * @param sampled whether the span and any subsequent child spans should be sampled
   */
  virtual void setSampled(bool sampled) PURE;

  /**
   * Retrieve a key's value from the span's baggage.
   * This baggage data could've been set by this span or any parent spans.
   * @param key baggage key
   * @return the baggage's value for the given input key
   */
  virtual std::string getBaggage(absl::string_view key) PURE;

  /**
   * Set a key/value pair in the current span's baggage.
   * All subsequent child spans will have access to this baggage.
   * @param key baggage key
   * @param key baggage value
   */
  virtual void setBaggage(absl::string_view key, absl::string_view value) PURE;

  /**
   * Retrieve the trace ID associated with this span.
   * The trace id may be generated for this span, propagated by parent spans, or
   * not created yet.
   * @return trace ID
   */
  virtual std::string getTraceId() const PURE;

  /**
   * Retrieve the span's identifier.
   * @return span ID as a hex string
   */
  virtual std::string getSpanId() const PURE;
};

/**
 * Tracing driver is responsible for span creation.
 */
class Driver {
public:
  virtual ~Driver() = default;

  /**
   * Start driver specific span.
   */
  virtual SpanPtr startSpan(const Config& config, TraceContext& trace_context,
                            const StreamInfo::StreamInfo& stream_info,
                            const std::string& operation_name,
                            Tracing::Decision tracing_decision) PURE;
};

using DriverPtr = std::unique_ptr<Driver>;
using DriverSharedPtr = std::shared_ptr<Driver>;

} // namespace Tracing
} // namespace Envoy
