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
  ~MockConfig();

  MOCK_CONST_METHOD0(operationName, OperationName());
  MOCK_CONST_METHOD0(requestHeadersForTags, const std::vector<Http::LowerCaseString>&());

  OperationName operation_name_{OperationName::Ingress};
  std::vector<Http::LowerCaseString> headers_;
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan();

  MOCK_METHOD1(setOperation, void(const std::string& operation));
  MOCK_METHOD2(setTag, void(const std::string& name, const std::string& value));
  MOCK_METHOD0(finishSpan, void());
  MOCK_METHOD1(injectContext, void(Http::HeaderMap& request_headers));

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
  ~MockHttpTracer();

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const RequestInfo::RequestInfo& request_info) override {
    return SpanPtr{startSpan_(config, request_headers, request_info)};
  }

  MOCK_METHOD3(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const RequestInfo::RequestInfo& request_info));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const std::string& operation_name, SystemTime start_time) override {
    return SpanPtr{startSpan_(config, request_headers, operation_name, start_time)};
  }

  MOCK_METHOD4(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const std::string& operation_name, SystemTime start_time));
};

} // namespace Tracing
} // namespace Envoy
