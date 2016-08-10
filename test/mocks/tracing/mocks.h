#pragma once

#include "envoy/tracing/http_tracer.h"

namespace Tracing {

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer();

  void addSink(HttpSinkPtr&& sink) override { addSink_(sink); }

  MOCK_METHOD1(addSink_, void(HttpSinkPtr& sink));
  MOCK_METHOD3(trace,
               void(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                    const Http::AccessLog::RequestInfo& request_info));
};

class MockHttpSink : public HttpSink {
public:
  MockHttpSink();
  ~MockHttpSink();

  MOCK_METHOD3(flushTrace,
               void(const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
                    const Http::AccessLog::RequestInfo& request_info));
};

} // Tracing