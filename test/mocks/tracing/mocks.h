#pragma once

#include "envoy/tracing/http_tracer.h"

namespace Tracing {

class MockTracingContext : public TracingContext {
public:
  MOCK_CONST_METHOD0(operationName, const std::string&());
};

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer();

  void addSink(HttpSinkPtr&& sink) override { addSink_(sink); }

  MOCK_METHOD1(addSink_, void(HttpSinkPtr& sink));
  MOCK_METHOD4(trace,
               void(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                    const Http::AccessLog::RequestInfo& request_info,
                    const TracingContext& tracing_context));
};

class MockHttpSink : public HttpSink {
public:
  MockHttpSink();
  ~MockHttpSink();

  MOCK_METHOD4(flushTrace,
               void(const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
                    const Http::AccessLog::RequestInfo& request_info,
                    const TracingContext& tracing_context));
};

} // Tracing