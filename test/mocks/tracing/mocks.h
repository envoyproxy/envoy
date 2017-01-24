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

  void initializeDriver(DriverPtr&& driver) override { initializeDriver_(driver); }

  MOCK_METHOD1(initializeDriver_, void(DriverPtr& driver));
  MOCK_METHOD3(startSpan, SpanPtr(const Config& config, const Http::HeaderMap& request_headers,
                                  const Http::AccessLog::RequestInfo& request_info));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  MOCK_METHOD2(startSpan, SpanPtr(const std::string& operation_name, SystemTime start_time));
};

} // Tracing