#pragma once

#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Tracing {

class HttpTracerUtility {
public:
  /**
   * Get string representation of the operation.
   * @param operation name to convert.
   * @return string representation of the operation.
   */
  static const std::string& toString(OperationName operation_name);

  /**
   * Request might be traceable if the request ID is traceable or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision shouldTraceRequest(const StreamInfo::StreamInfo& stream_info);

  /**
   * Adds information obtained from the downstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeDownstreamSpan(Span& span, const Http::RequestHeaderMap* request_headers,
                                     const Http::ResponseHeaderMap* response_headers,
                                     const Http::ResponseTrailerMap* response_trailers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config);

  /**
   * Adds information obtained from the upstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeUpstreamSpan(Span& span, const StreamInfo::StreamInfo& stream_info,
                                   const Config& tracing_config);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response headers.
   * NOOP if headers are nullptr.
   */
  static void onUpstreamResponseHeaders(Span& span,
                                        const Http::ResponseHeaderMap* response_headers);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response trailers.
   * NOOP if trailers are nullptr.
   */
  static void onUpstreamResponseTrailers(Span& span,
                                         const Http::ResponseTrailerMap* response_trailers);

private:
  static void setCommonTags(Span& span, const StreamInfo::StreamInfo& stream_info,
                            const Config& tracing_config);

  static const std::string IngressOperation;
  static const std::string EgressOperation;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const CustomTagMap* customTags() const override { return nullptr; }
  bool verbose() const override { return false; }
  uint32_t maxPathTagLength() const override { return Tracing::DefaultMaxPathTagLength; }
};

using EgressConfig = ConstSingleton<EgressConfigImpl>;

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  SpanPtr startSpan(const Config&, Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                    const Tracing::Decision) override {
    return SpanPtr{new NullSpan()};
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverSharedPtr driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::RequestHeaderMap& request_headers,
                    const StreamInfo::StreamInfo& stream_info,
                    const Tracing::Decision tracing_decision) override;

  DriverSharedPtr driverForTest() const { return driver_; }

private:
  DriverSharedPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

} // namespace Tracing
} // namespace Envoy
