#pragma once

#include "envoy/tracing/http_tracer.h"

namespace Tracing {

bool operator==(const TransportContext& lhs, const TransportContext& rhs) {
  return lhs.request_id_ == rhs.request_id_ && lhs.span_context_ == rhs.span_context_;
}

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

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override {
    return SpanPtr{startSpan_(config, request_headers, request_info)};
  }

  MOCK_METHOD3(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const Http::AccessLog::RequestInfo& request_info));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  SpanPtr startSpan(Http::HeaderMap& request_headers, const std::string& operation_name,
                    SystemTime start_time) override {
    return SpanPtr{startSpan_(request_headers, operation_name, start_time)};
  }

  MOCK_METHOD3(startSpan_, Span*(Http::HeaderMap& request_headers,
                                 const std::string& operation_name, SystemTime start_time));
};

} // Tracing