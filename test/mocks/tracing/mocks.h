#pragma once

#include <string>
#include <vector>

#include "envoy/tracing/trace_driver.h"
#include "envoy/tracing/tracer.h"
#include "envoy/tracing/tracer_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Tracing {

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig() override;

  MOCK_METHOD(OperationName, operationName, (), (const));
  MOCK_METHOD(const CustomTagMap*, customTags, (), (const));
  MOCK_METHOD(bool, verbose, (), (const));
  MOCK_METHOD(uint32_t, maxPathTagLength, (), (const));
  MOCK_METHOD(bool, spawnUpstreamSpan, (), (const));

  OperationName operation_name_{OperationName::Ingress};
  CustomTagMap custom_tags_;
  bool verbose_{false};
  bool spawn_upstream_span_{false};
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan() override;

  MOCK_METHOD(void, setOperation, (absl::string_view operation));
  MOCK_METHOD(void, setTag, (absl::string_view name, absl::string_view value));
  MOCK_METHOD(void, log, (SystemTime timestamp, const std::string& event));
  MOCK_METHOD(void, finishSpan, ());
  MOCK_METHOD(void, injectContext,
              (Tracing::TraceContext & request_headers, const Tracing::UpstreamContext& upstream));
  MOCK_METHOD(void, setSampled, (const bool sampled));
  MOCK_METHOD(void, setBaggage, (absl::string_view key, absl::string_view value));
  MOCK_METHOD(std::string, getBaggage, (absl::string_view key));
  MOCK_METHOD(std::string, getTraceId, (), (const));
  MOCK_METHOD(std::string, getSpanId, (), (const));

  SpanPtr spawnChild(const Config& config, const std::string& name,
                     SystemTime start_time) override {
    return SpanPtr{spawnChild_(config, name, start_time)};
  }

  MOCK_METHOD(Span*, spawnChild_,
              (const Config& config, const std::string& name, SystemTime start_time));
};

class MockTracer : public Tracer {
public:
  MockTracer();
  ~MockTracer() override;

  SpanPtr startSpan(const Config& config, TraceContext& trace_context,
                    const StreamInfo::StreamInfo& stream_info,
                    Tracing::Decision tracing_decision) override {
    return SpanPtr{startSpan_(config, trace_context, stream_info, tracing_decision)};
  }

  MOCK_METHOD(Span*, startSpan_,
              (const Config& config, TraceContext& trace_context,
               const StreamInfo::StreamInfo& stream_info, Tracing::Decision tracing_decision));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver() override;

  SpanPtr startSpan(const Config& config, TraceContext& trace_context,
                    const StreamInfo::StreamInfo& stream_info, const std::string& operation_name,
                    Tracing::Decision tracing_decision) override {
    return SpanPtr{
        startSpan_(config, trace_context, stream_info, operation_name, tracing_decision)};
  }

  MOCK_METHOD(Span*, startSpan_,
              (const Config& config, TraceContext& trace_context,
               const StreamInfo::StreamInfo& stream_info, const std::string& operation_name,
               Tracing::Decision tracing_decision));
};

class MockTracerManager : public TracerManager {
public:
  MockTracerManager();
  ~MockTracerManager() override;

  MOCK_METHOD(TracerSharedPtr, getOrCreateTracer, (const envoy::config::trace::v3::Tracing_Http*));
};

} // namespace Tracing
} // namespace Envoy
