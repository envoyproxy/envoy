#pragma once

#include "envoy/tracing/http_tracer.h"

namespace Tracing {

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  MOCK_CONST_METHOD0(operationName, const std::string&());
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan();

  MOCK_METHOD2(setTag, void(const std::string& name, const std::string& value));
  MOCK_METHOD0(finishSpan, void());
};

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer();

  SpanPtr startSpan(const Config& config, const Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override {
    return SpanPtr{startSpan_(config, request_headers, request_info)};
  }

  MOCK_METHOD3(startSpan_, Span*(const Config& config, const Http::HeaderMap& request_headers,
                                 const Http::AccessLog::RequestInfo& request_info));
  MOCK_METHOD2(inject, void(Span* active_span, Http::HeaderMap& request_headers));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  SpanPtr startSpan(const std::string& parent_context, const std::string& operation_name,
                    SystemTime start_time) override {
    return SpanPtr{startSpan_(parent_context, operation_name, start_time)};
  }

  MOCK_METHOD3(startSpan_, Span*(const std::string& parent_context,
                                 const std::string& operation_name, SystemTime start_time));
  MOCK_METHOD2(inject, void(Span* active_span, Http::HeaderMap& request_headers));
};

} // Tracing