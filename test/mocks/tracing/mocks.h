#pragma once

#include <string>
#include <vector>

#include "envoy/tracing/http_tracer.h"

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

  OperationName operation_name_{OperationName::Ingress};
  CustomTagMap custom_tags_;
  bool verbose_{false};
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan() override;

  MOCK_METHOD(void, setOperation, (absl::string_view operation));
  MOCK_METHOD2(setTag, void(absl::string_view name, absl::string_view value));
  MOCK_METHOD2(log, void(SystemTime timestamp, const std::string& event));
  MOCK_METHOD(void, finishSpan, ());
  MOCK_METHOD(void, injectContext, (Http::HeaderMap & request_headers));
  MOCK_METHOD(void, setSampled, (const bool sampled));

  SpanPtr spawnChild(const Config& config, const std::string& name,
                     SystemTime start_time) override {
    return SpanPtr{spawnChild_(config, name, start_time)};
  }

  MOCK_METHOD3(spawnChild_,
               Span*(const Config& config, const std::string& name, SystemTime start_time));
};

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer() override;

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const StreamInfo::StreamInfo& stream_info,
                    const Tracing::Decision tracing_decision) override {
    return SpanPtr{startSpan_(config, request_headers, stream_info, tracing_decision)};
  }

  MOCK_METHOD4(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const StreamInfo::StreamInfo& stream_info,
                                 const Tracing::Decision tracing_decision));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver() override;

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const std::string& operation_name, SystemTime start_time,
                    const Tracing::Decision tracing_decision) override {
    return SpanPtr{
        startSpan_(config, request_headers, operation_name, start_time, tracing_decision)};
  }

  MOCK_METHOD5(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const std::string& operation_name, SystemTime start_time,
                                 const Tracing::Decision tracing_decision));
};

} // namespace Tracing
} // namespace Envoy
