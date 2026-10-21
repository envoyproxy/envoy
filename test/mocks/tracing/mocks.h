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

  MOCK_METHOD(OperationName, operationName, (), (const, override));
  MOCK_METHOD(void, modifySpan, (Span&, bool), (const, override));
  MOCK_METHOD(bool, verbose, (), (const, override));
  MOCK_METHOD(uint32_t, maxPathTagLength, (), (const, override));
  MOCK_METHOD(bool, spawnUpstreamSpan, (), (const, override));
  MOCK_METHOD(bool, noContextPropagation, (), (const, override));

  OperationName operation_name_{OperationName::Ingress};
  bool verbose_{false};
  bool spawn_upstream_span_{false};
  bool no_context_propagation_{false};
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan() override;

  MOCK_METHOD(void, setOperation, (absl::string_view operation), (override));
  MOCK_METHOD(void, setTag, (absl::string_view name, absl::string_view value), (override));
  MOCK_METHOD(void, setTypedTag,
              (absl::string_view name, absl::string_view value, TagValueType type), (override));
  MOCK_METHOD(void, log, (SystemTime timestamp, const std::string& event), (override));
  MOCK_METHOD(void, finishSpan, (), (override));
  MOCK_METHOD(void, injectContext,
              (Tracing::TraceContext & request_headers, const Tracing::UpstreamContext& upstream),
              (override));
  MOCK_METHOD(void, setSampled, (bool sampled), (override));
  MOCK_METHOD(void, disableLocalDecision, (), (override));
  MOCK_METHOD(bool, exportedSpan, (), (const, override));
  MOCK_METHOD(bool, useLocalDecision, (), (const, override));
  MOCK_METHOD(void, setBaggage, (absl::string_view key, absl::string_view value), (override));
  MOCK_METHOD(std::string, getBaggage, (absl::string_view key), (override));
  MOCK_METHOD(std::string, getTraceId, (), (const, override));
  MOCK_METHOD(std::string, getSpanId, (), (const, override));

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

  MOCK_METHOD(TracerSharedPtr, getOrCreateTracer, (const envoy::config::trace::v3::Tracing_Http*),
              (override));
};

} // namespace Tracing
} // namespace Envoy
