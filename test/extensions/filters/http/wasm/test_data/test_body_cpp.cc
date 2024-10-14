// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

class BodyRootContext : public RootContext {
public:
  explicit BodyRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}
};

class BodyContext : public Context {
public:
  explicit BodyContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
  FilterDataStatus onRequestBody(size_t body_buffer_length, bool end_of_stream) override;
  FilterHeadersStatus onResponseHeaders(uint32_t, bool) override;
  FilterDataStatus onResponseBody(size_t body_buffer_length, bool end_of_stream) override;

private:
  BodyRootContext* root() { return static_cast<BodyRootContext*>(Context::root()); }
  static void logBody(WasmBufferType type);
  FilterDataStatus onBody(WasmBufferType type, size_t buffer_length, bool end);
  std::string body_op_;
  int num_chunks_ = 0;
};

static RegisterContextFactory register_BodyContext(CONTEXT_FACTORY(BodyContext),
                                                   ROOT_FACTORY(BodyRootContext), "body");

void BodyContext::logBody(WasmBufferType type) {
  size_t buffered_size;
  uint32_t flags;
  getBufferStatus(type, &buffered_size, &flags);
  auto body = getBufferBytes(type, 0, buffered_size);
  logError(std::string("onBody ") + std::string(body->view()));
}

FilterDataStatus BodyContext::onBody(WasmBufferType type, size_t buffer_length,
                                     bool end_of_stream) {
  size_t size;
  uint32_t flags;
  if (body_op_ == "ReadBody") {
    logBody(type);
    return FilterDataStatus::Continue;

  } else if (body_op_ == "PrependAndAppendToBody") {
    setBuffer(type, 0, 0, "prepend.");
    getBufferStatus(type, &size, &flags);
    setBuffer(type, size, 0, ".append");
    logBody(type);
    return FilterDataStatus::StopIterationNoBuffer;

  } else if (body_op_ == "ReplaceBody") {
    setBuffer(type, 0, buffer_length, "replace");
    logBody(type);
    return FilterDataStatus::StopIterationAndWatermark;

  } else if (body_op_ == "PartialReplaceBody") {
    setBuffer(type, 0, 1, "partial.replace.");
    logBody(type);
    return FilterDataStatus::Continue;

  } else if (body_op_ == "RemoveBody") {
    setBuffer(type, 0, buffer_length, "");
    logBody(type);
    return FilterDataStatus::Continue;

  } else if (body_op_ == "PartialRemoveBody") {
    setBuffer(type, 0, 1, "");
    logBody(type);
    return FilterDataStatus::Continue;

  } else if (body_op_ == "BufferBody") {
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "PrependAndAppendToBufferedBody") {
    setBuffer(type, 0, 0, "prepend.");
    getBufferStatus(type, &size, &flags);
    setBuffer(type, size, 0, ".append");
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "ReplaceBufferedBody") {
    setBuffer(type, 0, buffer_length, "replace");
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "PartialReplaceBufferedBody") {
    setBuffer(type, 0, 1, "partial.replace.");
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "RemoveBufferedBody") {
    setBuffer(type, 0, buffer_length, "");
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "PartialRemoveBufferedBody") {
    setBuffer(type, 0, 1, "");
    logBody(type);
    return end_of_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "BufferTwoBodies") {
    logBody(type);
    num_chunks_++;
    if (end_of_stream || num_chunks_ > 2) {
      return FilterDataStatus::Continue;
    }
    return FilterDataStatus::StopIterationAndBuffer;

  } else if (body_op_ == "SetEndOfBodies") {
    logBody(type);
    if (end_of_stream) {
      getBufferStatus(type, &size, &flags);
      setBuffer(type, size, 0, ".end");
      return FilterDataStatus::Continue;
    }
    return FilterDataStatus::StopIterationAndBuffer;

  } else {
    // This is a test and the test was configured incorrectly.
    logError("Invalid body test op " + body_op_);
    abort();
  }
  return FilterDataStatus::Continue;
}

FilterHeadersStatus BodyContext::onRequestHeaders(uint32_t, bool) {
  body_op_ = getRequestHeader("x-test-operation")->toString();
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus BodyContext::onResponseHeaders(uint32_t, bool) {
  body_op_ = getResponseHeader("x-test-operation")->toString();
  CHECK_RESULT(replaceResponseHeader("x-test-operation", body_op_));
  return FilterHeadersStatus::Continue;
}

FilterDataStatus BodyContext::onRequestBody(size_t body_buffer_length, bool end_of_stream) {
  return onBody(WasmBufferType::HttpRequestBody, body_buffer_length, end_of_stream);
}

FilterDataStatus BodyContext::onResponseBody(size_t body_buffer_length, bool end_of_stream) {
  return onBody(WasmBufferType::HttpResponseBody, body_buffer_length, end_of_stream);
}

END_WASM_PLUGIN
