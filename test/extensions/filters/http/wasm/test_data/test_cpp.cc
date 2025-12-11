// NOLINT(namespace-envoy)
#include <memory>
#include <string>
#include <unordered_map>
#include "test/extensions/filters/http/wasm/test_data/test.pb.h"

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_lite.h"
#include "source/extensions/common/wasm/ext/envoy_proxy_wasm_api.h"
#include "source/extensions/common/wasm/ext/declare_property.pb.h"
#include "source/extensions/common/wasm/ext/verify_signature.pb.h"
#include "source/extensions/common/wasm/ext/sign.pb.h"
#else
#include "source/extensions/common/wasm/ext/envoy_null_plugin.h"
#include "absl/base/casts.h"
#endif

START_WASM_PLUGIN(HttpWasmTestCpp)

#include "contrib/proxy_expr.h"

class TestRootContext : public RootContext {
public:
  explicit TestRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}

  bool onStart(size_t configuration_size) override;
  void onTick() override;
  bool onConfigure(size_t) override;

  std::string test_;
  uint32_t stream_context_id_;
};

class TestContext : public Context {
public:
  explicit TestContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
  FilterTrailersStatus onRequestTrailers(uint32_t) override;
  FilterHeadersStatus onResponseHeaders(uint32_t, bool) override;
  FilterTrailersStatus onResponseTrailers(uint32_t) override;
  FilterDataStatus onRequestBody(size_t body_buffer_length, bool end_of_stream) override;
  FilterDataStatus onResponseBody(size_t body_buffer_length, bool end_of_stream) override;
  void onLog() override;
  void onDone() override;

private:
  TestRootContext* root() { return static_cast<TestRootContext*>(Context::root()); }
};

static RegisterContextFactory register_TestContext(CONTEXT_FACTORY(TestContext),
                                                   ROOT_FACTORY(TestRootContext));

bool TestRootContext::onStart(size_t configuration_size) {
  test_ = getBufferBytes(WasmBufferType::VmConfiguration, 0, configuration_size)->toString();
  return true;
}

bool TestRootContext::onConfigure(size_t size) {
  if (size > 0 &&
      getBufferBytes(WasmBufferType::PluginConfiguration, 0, size)->toString() == "invalid") {
    return false;
  }
  if (test_ == "property") {
    {
      // Many properties are not available in the root context.
      const std::vector<std::string> properties = {
          "string_state",     "metadata",   "request",        "response",    "connection",
          "connection_id",    "upstream",   "source",         "destination", "cluster_name",
          "cluster_metadata", "route_name", "route_metadata", "upstream_host_metadata",
          "filter_state", "listener_direction" ,"listener_metadata",
      };
      for (const auto& property : properties) {
        if (getProperty({property}).has_value()) {
          logWarn("getProperty should not return a value in the root context");
        }
      }
    }
    {
      // Some properties are defined in the root context.
      std::vector<std::pair<std::vector<std::string>, std::string>> properties = {
          {{"plugin_name"}, "plugin_name"},
          {{"plugin_vm_id"}, "vm_id"},
          {{"xds", "node", "metadata", "istio.io/metadata"}, "sample_data"},
      };
      for (const auto& property : properties) {
        std::string value;
        if (!getValue(property.first, &value)) {
          logWarn("getValue should provide a value in the root context: " + property.second);
        }
        if (value != property.second) {
          logWarn("getValue returned " + value + ", expect " + property.second);
        }
      }
    }
  }
  return true;
}

FilterHeadersStatus TestContext::onRequestHeaders(uint32_t, bool) {
  root()->stream_context_id_ = id();
  auto test = root()->test_;
  if (test == "headers") {
    std::string msg = "";
    if (auto value = std::getenv("ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV")) {
      msg += "ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV: " + std::string(value);
    }
    if (auto value = std::getenv("ENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV")) {
      msg += "\nENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV: " + std::string(value);
    }
    if (!msg.empty()) {
      logTrace(msg);
    }
    logDebug(std::string("onRequestHeaders ") + std::to_string(id()) + std::string(" ") + test);
    auto path = getRequestHeader(":path");
    logInfo(std::string("header path ") + std::string(path->view()));
    std::string protocol;
    addRequestHeader("newheader", "newheadervalue");
    auto server = getRequestHeader("server");
    replaceRequestHeader("server", "envoy-wasm");
    auto r = addResponseHeader("bad", "bad");
    if (r != WasmResult::BadArgument) {
      logWarn("unexpected success of addResponseHeader");
    }
    if (addResponseTrailer("bad", "bad") != WasmResult::BadArgument) {
      logWarn("unexpected success of addResponseTrailer");
    }
    if (removeResponseTrailer("bad") != WasmResult::BadArgument) {
      logWarn("unexpected success of remoteResponseTrailer");
    }
    size_t size;
    if (getRequestHeaderSize(&size) != WasmResult::Ok) {
      logWarn("unexpected failure of getRequestHeaderMapSize");
    }
    if (getResponseHeaderSize(&size) != WasmResult::BadArgument) {
      logWarn("unexpected success of getResponseHeaderMapSize");
    }
    if (server->view() == "envoy-wasm-pause") {
      return FilterHeadersStatus::StopIteration;
    } else if (server->view() == "envoy-wasm-end-stream") {
      return FilterHeadersStatus::ContinueAndEndStream;
    } else if (server->view() == "envoy-wasm-stop-buffer") {
      return FilterHeadersStatus::StopAllIterationAndBuffer;
    } else if (server->view() == "envoy-wasm-stop-watermark") {
      return FilterHeadersStatus::StopAllIterationAndWatermark;
    } else {
      return FilterHeadersStatus::Continue;
    }
  } else if (test == "metadata") {
    std::string value;
    if (!getValue({"xds", "node", "metadata", "wasm_node_get_key"}, &value)) {
      logDebug("missing node metadata");
    }
    auto r = setFilterStateStringValue("wasm_request_set_key", "wasm_request_set_value");
    if (r != WasmResult::Ok) {
      logDebug(toString(r));
    }
    auto path = getRequestHeader(":path");
    logInfo(std::string("header path ") + path->toString());
    addRequestHeader("newheader", "newheadervalue");
    replaceRequestHeader("server", "envoy-wasm");

    {
      const std::string expr = R"("server is " + request.headers["server"])";
      uint32_t token = 0;
      if (WasmResult::Ok != createExpression(expr, &token)) {
        logError("expr_create error");
      } else {
        std::string eval_result;
        if (!evaluateExpression(token, &eval_result)) {
          logError("expr_eval error");
        } else {
          logInfo(eval_result);
        }
        if (WasmResult::Ok != exprDelete(token)) {
          logError("failed to delete an expression");
        }
      }
    }

    {
      // Validate a valid CEL expression
      const std::string expr = R"(
  envoy.config.core.v3.GrpcService{
    envoy_grpc: envoy.config.core.v3.GrpcService.EnvoyGrpc {
      cluster_name: "test"
    }
  })";
      uint32_t token = 0;
      if (WasmResult::Ok != createExpression(expr, &token)) {
        logError("expr_create error");
      } else {
        GrpcService eval_result;
        if (!evaluateMessage(token, &eval_result)) {
          logError("expr_eval error");
        } else {
          logInfo("grpc service: " + eval_result.envoy_grpc().cluster_name());
        }
        if (WasmResult::Ok != exprDelete(token)) {
          logError("failed to delete an expression");
        }
      }
    }

    {
      // Create a syntactically wrong CEL expression
      uint32_t token = 0;
      if (createExpression("/ /", &token) != WasmResult::BadArgument) {
        logError("expect an error on a syntactically wrong expressions");
      }
    }

    {
      // Create an invalid CEL expression
      uint32_t token = 0;
      if (createExpression("_&&_(a, b, c)", &token) != WasmResult::BadArgument) {
        logError("expect an error on invalid expressions");
      }
    }

    {
      // Evaluate a bad token
      std::string result;
      uint64_t token = 0;
      if (evaluateExpression(token, &result)) {
        logError("expect an error on invalid token in evaluate");
      }
    }

    {
      // Evaluate a missing token
      std::string result;
      uint32_t token = 0xFFFFFFFF;
      if (evaluateExpression(token, &result)) {
        logError("expect an error on unknown token in evaluate");
      }
      // Delete a missing token
      if (exprDelete(token) != WasmResult::Ok) {
        logError("expect no error on unknown token in delete expression");
      }
    }

    {
      // Evaluate two expressions to an error
      uint32_t token1 = 0;
      if (createExpression("1/0", &token1) != WasmResult::Ok) {
        logError("unexpected error on division by zero expression");
      }
      uint32_t token2 = 0;
      if (createExpression("request.duration.size", &token2) != WasmResult::Ok) {
        logError("unexpected error on integer field access expression");
      }
      std::string result;
      if (evaluateExpression(token1, &result)) {
        logError("expect an error on division by zero");
      }
      if (evaluateExpression(token2, &result)) {
        logError("expect an error on integer field access expression");
      }
      if (exprDelete(token1) != WasmResult::Ok) {
        logError("failed to delete an expression");
      }
      if (exprDelete(token2) != WasmResult::Ok) {
        logError("failed to delete an expression");
      }
    }

    {
      int64_t dur;
      if (getValue({"request", "duration"}, &dur)) {
        logInfo("duration is " + std::to_string(dur));
      } else {
        logError("failed to get request duration");
      }
    }

    return FilterHeadersStatus::Continue;
  }
  return FilterHeadersStatus::Continue;
}

FilterTrailersStatus TestContext::onRequestTrailers(uint32_t) {
  auto request_trailer = getRequestTrailer("bogus-trailer");
  if (request_trailer && !request_trailer->view().empty()) {
    logWarn("request bogus-trailer found");
  }
  CHECK_RESULT(replaceRequestTrailer("new-trailer", "value"));
  CHECK_RESULT(removeRequestTrailer("x"));
  // Not available yet.
  replaceResponseTrailer("new-trailer", "value");
  auto response_trailer = getResponseTrailer("bogus-trailer");
  if (response_trailer && !response_trailer->view().empty()) {
    logWarn("request bogus-trailer found");
  }
  return FilterTrailersStatus::Continue;
}

FilterHeadersStatus TestContext::onResponseHeaders(uint32_t, bool) {
  root()->stream_context_id_ = id();
  auto test = root()->test_;
  if (test == "headers") {
    CHECK_RESULT(addResponseHeader("test-status", "OK"));
  }
  return FilterHeadersStatus::Continue;
}

FilterTrailersStatus TestContext::onResponseTrailers(uint32_t) {
  auto value = getResponseTrailer("bogus-trailer");
  if (value && !value->view().empty()) {
    logWarn("response bogus-trailer found");
  }
  CHECK_RESULT(replaceResponseTrailer("new-trailer", "value"));
  return FilterTrailersStatus::StopIteration;
}

FilterDataStatus TestContext::onRequestBody(size_t body_buffer_length, bool end_of_stream) {
  auto test = root()->test_;
  if (test == "headers") {
    auto body = getBufferBytes(WasmBufferType::HttpRequestBody, 0, body_buffer_length);
    logError(std::string("onBody ") + std::string(body->view()));
    if (end_of_stream) {
      CHECK_RESULT(addRequestTrailer("newtrailer", "request"));
    }
  } else if (test == "metadata") {
    std::string value;
    if (!getValue({"xds", "node", "metadata", "wasm_node_get_key"}, &value)) {
      logDebug("missing node metadata");
    }
    logError(std::string("onBody ") + value);
    std::string request_string;
    std::string request_string2;
    if (!getValue(
            {"metadata", "filter_metadata", "envoy.filters.http.wasm", "wasm_request_get_key"},
            &request_string)) {
      logDebug("missing request metadata");
    }
    if (!getValue(
            {"metadata", "filter_metadata", "envoy.filters.http.wasm", "wasm_request_get_key"},
            &request_string2)) {
      logDebug("missing request metadata");
    }
    logTrace(std::string("Struct ") + request_string + " " + request_string2);
    return FilterDataStatus::Continue;
  }
  return FilterDataStatus::Continue;
}

FilterDataStatus TestContext::onResponseBody(size_t, bool end_of_stream) {
  auto test = root()->test_;
  if (test == "headers") {
    if (end_of_stream) {
      CHECK_RESULT(addResponseTrailer("newtrailer", "response"));
    }
  }
  return FilterDataStatus::Continue;
}

void TestContext::onLog() {
  auto test = root()->test_;
  if (test == "headers") {
    auto path = getRequestHeader(":path");
    auto status = getResponseHeader(":status");
    logWarn("onLog " + std::to_string(id()) + " " + std::string(path->view()) + " " +
            std::string(status->view()));
    auto response_header = getResponseHeader("bogus-header");
    if (response_header && !response_header->view().empty()) {
      logWarn("response bogus-header found");
    }
    auto response_trailer = getResponseTrailer("bogus-trailer");
    if (response_trailer && !response_trailer->view().empty()) {
      logWarn("response bogus-trailer found");
    }
    auto request_trailer = getRequestTrailer("error-details");
    if (request_trailer && !request_trailer->view().empty()) {
      logWarn("request bogus-trailer found");
    }
  } else if (test == "cluster_metadata") {
    std::string cluster_metadata;
    if (getValue({"xds", "cluster_metadata", "filter_metadata", "namespace", "key"}, &cluster_metadata)) {
      logWarn("cluster metadata: " + cluster_metadata);
    }
  } else if (test == "property") {
    setFilterState("wasm_state", "wasm_value");
    auto path = getRequestHeader(":path");
    if (path->view() == "/test_context") {
      logWarn("request.path: " + getProperty({"request", "path"}).value()->toString());
      logWarn("node.metadata: " +
              getProperty({"xds", "node", "metadata", "istio.io/metadata"}).value()->toString());
      logWarn("metadata: " + getProperty({"metadata", "filter_metadata", "envoy.filters.http.wasm",
                                          "wasm_request_get_key"})
                                 .value()
                                 ->toString());
      int64_t responseCode;
      if (getValue({"response", "code"}, &responseCode)) {
        logWarn("response.code: " + std::to_string(responseCode));
      }
      std::string upstream_host_metadata;
      if (getValue({"xds", "upstream_host_metadata", "filter_metadata", "namespace", "key"}, &upstream_host_metadata)) {
        logWarn("upstream host metadata: " + upstream_host_metadata);
      }
      logWarn("state: " + getProperty({"wasm_state"}).value()->toString());
    } else {
      logWarn("onLog " + std::to_string(id()) + " " + std::string(path->view()));
    }

    // Wasm state property set and read validation for {i: 1337}
    // Generated using the following input.json:
    // {
    //   "i": 1337
    // }
    // flatc -b schema.fbs input.json
    {
      static const char data[24] = {0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
                                    0x0c, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
                                    0x39, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      if (WasmResult::Ok != setFilterState("structured_state", std::string_view(data, 24))) {
        logWarn("setProperty(structured_state) failed");
      }
      int64_t value = 0;
      if (!getValue({"structured_state", "i"}, &value)) {
        logWarn("getProperty(structured_state) failed");
      }
      if (value != 1337) {
        logWarn("getProperty(structured_state) returned " + std::to_string(value));
      }
      std::string buffer;
      if (!getValue({"structured_state"}, &buffer)) {
        logWarn("getValue for structured_state should not fail");
      }
      if (buffer.size() != 24) {
        logWarn("getValue for structured_state should return the buffer");
      }
    }
    {
      if (setFilterState("string_state", "unicorns") != WasmResult::Ok) {
        logWarn("setProperty(string_state) failed");
      }
      std::string value;
      if (!getValue({"string_state"}, &value)) {
        logWarn("getProperty(string_state) failed");
      }
      if (value != "unicorns") {
        logWarn("getProperty(string_state) returned " + value);
      }
    }
    {
      // access via "filter_state" property
      std::string value;
      if (!getValue({"filter_state", "wasm.string_state"}, &value)) {
        logWarn("accessing via filter_state failed");
      }
      if (value != "unicorns") {
        logWarn("unexpected value: " + value);
      }
    }
    {
      // attempt to write twice for a read only wasm state
      if (setFilterState("string_state", "ponies") == WasmResult::Ok) {
        logWarn("expected second setProperty(string_state) to fail");
      }
      std::string value;
      if (!getValue({"string_state"}, &value)) {
        logWarn("getProperty(string_state) failed");
      }
      if (value != "unicorns") {
        logWarn("getProperty(string_state) returned " + value);
      }
    }
    {
      if (setFilterState("bytes_state", "ponies") != WasmResult::Ok) {
        logWarn("setProperty(bytes_state) failed");
      }
      std::string value;
      if (!getValue({"bytes_state"}, &value)) {
        logWarn("getProperty(bytes_state) failed");
      }
      if (value != "ponies") {
        logWarn("getProperty(bytes_state) returned " + value);
      }
    }
    {
      wasmtest::TestProto test_proto;
      uint32_t i = 53;
      test_proto.set_i(i);
      double j = 13.0;
      test_proto.set_j(j);
      bool k = true;
      test_proto.set_k(k);
      std::string s = "centaur";
      test_proto.set_s(s);
      test_proto.mutable_t()->set_seconds(2);
      test_proto.mutable_t()->set_nanos(3);
      test_proto.add_l("abc");
      test_proto.add_l("xyz");
      (*test_proto.mutable_m())["a"] = "b";

      // validate setting a filter state
      std::string in;
      test_proto.SerializeToString(&in);
      if (setFilterState("protobuf_state", in) != WasmResult::Ok) {
        logWarn("setProperty(protobuf_state) failed");
      }
      // validate uint field
      uint64_t i2;
      if (!getValue({"protobuf_state", "i"}, &i2) || i2 != i) {
        logWarn("uint field returned " + std::to_string(i2));
      }

      // validate double field
      double j2;
      if (!getValue({"protobuf_state", "j"}, &j2) || j2 != j) {
        logWarn("double field returned " + std::to_string(j2));
      }

      // validate bool field
      bool k2;
      if (!getValue({"protobuf_state", "k"}, &k2) || k2 != k) {
        logWarn("bool field returned " + std::to_string(k2));
      }

      // validate string field
      std::string s2;
      if (!getValue({"protobuf_state", "s"}, &s2) || s2 != s) {
        logWarn("string field returned " + s2);
      }

      // validate timestamp field
      int64_t t;
      if (!getValue({"protobuf_state", "t"}, &t) || t != 2000000003ull) {
        logWarn("timestamp field returned " + std::to_string(t));
      }

      // validate malformed field
      std::string a;
      if (getValue({"protobuf_state", "a"}, &a)) {
        logWarn("expect serialization error for malformed type_url string, got " + a);
      }

      // validate null field
      std::string b;
      if (!getValue({"protobuf_state", "b"}, &b) || !b.empty()) {
        logWarn("null field returned " + b);
      }

      // validate list field
      auto l = getProperty({"protobuf_state", "l"});
      if (l.has_value()) {
        auto pairs = l.value()->pairs();
        if (pairs.size() != 2 || pairs[0].first != "abc" || pairs[1].first != "xyz") {
          logWarn("list field did not return the expected value");
        }
      } else {
        logWarn("list field returned none");
      }

      // validate map field
      auto m = getProperty({"protobuf_state", "m"});
      if (m.has_value()) {
        auto pairs = m.value()->pairs();
        if (pairs.size() != 1 || pairs[0].first != "a" || pairs[0].second != "b") {
          logWarn("map field did not return the expected value: " + std::to_string(pairs.size()));
        }
      } else {
        logWarn("map field returned none");
      }

      // validate entire message
      std::string buffer;
      if (!getValue({"protobuf_state"}, &buffer)) {
        logWarn("getValue for protobuf_state should not fail");
      }
      if (buffer.size() != in.size()) {
        logWarn("getValue for protobuf_state should return the buffer");
      }
    }
    {
      // Some properties are not available in the stream context.
      const std::vector<std::string> properties = {"xxx", "request", "route_name", "node"};
      for (const auto& property : properties) {
        if (getProperty({property, "xxx"}).has_value()) {
          logWarn("getProperty should not return a value in the root context");
        }
      }
    }
    {
      // Some properties are defined in the stream context.
      std::vector<std::pair<std::vector<std::string>, std::string>> properties = {
          {{"plugin_name"}, "plugin_name"},
          {{"plugin_vm_id"}, "vm_id"},
          {{"connection_id"}, std::string("\x4\0\0\0\0\0\0\0\0", 8)},
          {{"connection", "requested_server_name"}, "w3.org"},
          {{"source", "address"}, "127.0.0.1:0"},
          {{"destination", "address"}, "127.0.0.2:0"},
          {{"upstream", "address"}, "10.0.0.1:443"},
      };
      for (const auto& property : properties) {
        std::string value;
        if (!getValue(property.first, &value)) {
          logWarn("getValue should provide a value in the root context: " + property.second);
        }
        if (value != property.second) {
          logWarn("getValue returned " + value + ", expect " + property.second);
        }
      }
    }
  }
}

void TestContext::onDone() {
  auto test = root()->test_;
  if (test == "headers") {
    logWarn("onDone " + std::to_string(id()));
  }
}

void TestRootContext::onTick() {
  if (test_ == "headers") { // NOLINT(clang-analyzer-optin.portability.UnixAPI)
    getContext(stream_context_id_)->setEffectiveContext();
    replaceRequestHeader("server", "envoy-wasm-continue");
    continueRequest();
    if (!getBufferBytes(WasmBufferType::PluginConfiguration, 0, 1)->view().empty()) {
      logDebug("unexpectd success of getBufferBytes PluginConfiguration");
    }
  } else if (test_ == "metadata") { // NOLINT(clang-analyzer-optin.portability.UnixAPI)
    std::string value;
    if (!getValue({"xds", "node", "metadata", "wasm_node_get_key"}, &value)) { // NOLINT(clang-analyzer-optin.portability.UnixAPI)
      logDebug("missing node metadata");
    }
    logDebug(std::string("onTick ") + value);

    std::string list_value;
    if (!getValue({"xds", "node", "metadata", "wasm_node_list_key", "0"}, &list_value)) {
      logDebug("missing node metadata list value");
    }
    if (list_value != "wasm_node_get_value") {
      logWarn("unexpected list value: " + list_value);
    }
    if (getValue({"xds", "node", "metadata", "wasm_node_list_key", "bad_key"}, &list_value)) {
      logDebug("unexpected list value for a bad_key");
    }
    if (getValue({"xds", "node", "metadata", "wasm_node_list_key", "1"}, &list_value)) {
      logDebug("unexpected list value outside the range");
    }
  } else if (test_ == "property") {
    uint64_t t;
    if (WasmResult::Ok != proxy_get_current_time_nanoseconds(&t)) {
      logError(std::string("bad proxy_get_current_time_nanoseconds result"));
    }
    std::string function = "declare_property";
    {
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      args.set_name("structured_state");
      args.set_type(envoy::source::extensions::common::wasm::WasmType::FlatBuffers);
      args.set_span(envoy::source::extensions::common::wasm::LifeSpan::DownstreamConnection);
      // Reflection flatbuffer for a simple table {i : int64}.
      // Generated using the following schema.fbs:
      //
      // namespace Wasm.Common;
      // table T {
      //   i: int64;
      // }
      // root_type T;
      //
      // flatc --cpp --bfbs-gen-embed schema.fbs
      static const char bfbsData[192] = {
          0x18, 0x00, 0x00, 0x00, 0x42, 0x46, 0x42, 0x53, 0x10, 0x00, 0x1C, 0x00, 0x04, 0x00, 0x08,
          0x00, 0x0C, 0x00, 0x10, 0x00, 0x14, 0x00, 0x18, 0x00, 0x10, 0x00, 0x00, 0x00, 0x30, 0x00,
          0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x34,
          0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x00, 0x04, 0x00,
          0x08, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08,
          0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00,
          0x0D, 0x00, 0x00, 0x00, 0x57, 0x61, 0x73, 0x6D, 0x2E, 0x43, 0x6F, 0x6D, 0x6D, 0x6F, 0x6E,
          0x2E, 0x54, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x12, 0x00, 0x08, 0x00, 0x0C, 0x00, 0x00, 0x00,
          0x06, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00, 0x0C,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x08, 0x00, 0x07, 0x00, 0x06, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00};
      args.set_schema(bfbsData, 192);
      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok != proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        logError("declare_property failed for flatbuffers");
      }
      ::free(out);
    }
    {
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      args.set_name("string_state");
      args.set_type(envoy::source::extensions::common::wasm::WasmType::String);
      args.set_span(envoy::source::extensions::common::wasm::LifeSpan::FilterChain);
      args.set_readonly(true);
      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok != proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        logError("declare_property failed for strings");
      }
      ::free(out);
    }
    {
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      args.set_name("bytes_state");
      args.set_type(envoy::source::extensions::common::wasm::WasmType::Bytes);
      args.set_span(envoy::source::extensions::common::wasm::LifeSpan::DownstreamRequest);
      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok != proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        logError("declare_property failed for bytes");
      }
      ::free(out);
    }
    {
      // double declaration of "bytes_state" should return BAD_ARGUMENT
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      args.set_name("bytes_state");
      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::BadArgument != proxy_call_foreign_function(function.data(), function.size(),
                                                                 in.data(), in.size(), &out,
                                                                 &out_size)) {
        logError("declare_property must fail for double declaration");
      }
      ::free(out);
    }
    {
      envoy::source::extensions::common::wasm::DeclarePropertyArguments args;
      args.set_name("protobuf_state");
      args.set_type(envoy::source::extensions::common::wasm::WasmType::Protobuf);
      args.set_span(envoy::source::extensions::common::wasm::LifeSpan::DownstreamRequest);
      args.set_schema("type.googleapis.com/wasmtest.TestProto");
      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok != proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        logError("declare_property failed for protobuf");
      }
      ::free(out);
    }
    {
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(),
                                                        function.data(), function.size(), &out,
                                                        &out_size)) {
        logError("expected declare_property to fail");
      }
      ::free(out);
    }
    {
      // setting a filter state in root context returns NOT_FOUND
      if (setFilterState("string_state", "unicorns") != WasmResult::NotFound) {
        logWarn("setProperty(string_state) should fail in root context");
      }
    }
  } else if (test_ == "verify_signature") {
      std::string function = "verify_signature";

      static const std::string data = "hello";
      static const std::vector<uint8_t> key = {48, 130, 1, 34, 48, 13, 6, 9, 42, 134, 72, 134, 247, 13, 1, 1, 1, 5, 0, 3, 130, 1, 15, 0, 48, 130, 1, 10, 2, 130, 1, 1, 0, 167, 71, 18, 102, 208, 29, 22, 3, 8, 215, 52, 9, 192, 111, 46, 141, 53, 197, 49, 196, 88, 211, 228, 128, 233, 243, 25, 24, 71, 208, 98, 236, 92, 207, 247, 188, 81, 233, 73, 213, 242, 195, 84, 12, 24, 154, 78, 202, 30, 134, 51, 166, 44, 242, 208, 146, 49, 1, 194, 126, 56, 1, 62, 113, 222, 154, 233, 26, 112, 72, 73, 191, 247, 251, 226, 206, 91, 244, 189, 102, 111, 217, 115, 17, 2, 165, 49, 147, 254, 90, 154, 90, 80, 100, 79, 248, 177, 24, 63, 168, 151, 100, 101, 152, 202, 173, 34, 163, 127, 149, 68, 81, 8, 54, 55, 43, 68, 197, 140, 152, 88, 111, 183, 20, 70, 41, 205, 140, 148, 121, 89, 45, 153, 109, 50, 255, 109, 57, 92, 11, 132, 66, 236, 90, 161, 239, 128, 81, 82, 158, 160, 227, 117, 136, 60, 239, 199, 44, 4, 227, 96, 180, 239, 143, 87, 96, 101, 5, 137, 202, 129, 73, 24, 246, 120, 238, 227, 155, 136, 77, 90, 248, 19, 106, 150, 48, 166, 204, 12, 222, 21, 125, 200, 224, 15, 57, 84, 6, 40, 213, 243, 53, 178, 195, 108, 84, 199, 200, 188, 55, 56, 166, 178, 26, 207, 248, 21, 64, 90, 250, 40, 229, 24, 63, 85, 13, 172, 25, 171, 207, 17, 69, 167, 249, 206, 217, 135, 219, 104, 14, 74, 34, 156, 172, 117, 222, 227, 71, 236, 158, 188, 225, 252, 61, 187, 187, 2, 3, 1, 0, 1};
      std::string key_str(key.begin(), key.end());
      static const std::vector<uint8_t> signature = {52, 90, 195, 161, 103, 85, 143, 79, 56, 122, 129, 194, 214, 66, 52, 217, 1, 167, 206, 170, 84, 77, 183, 121, 210, 247, 151, 176, 234, 78, 248, 81, 183, 64, 144, 90, 99, 226, 244, 213, 175, 66, 206, 224, 147, 162, 156, 113, 85, 219, 154, 99, 211, 212, 131, 224, 239, 148, 143, 90, 197, 28, 228, 225, 10, 58, 102, 6, 253, 147, 239, 104, 238, 71, 179, 12, 55, 73, 17, 3, 3, 148, 89, 18, 47, 120, 225, 199, 234, 113, 161, 165, 234, 36, 187, 101, 25, 188, 160, 44, 140, 153, 21, 254, 139, 226, 73, 39, 201, 24, 18, 161, 61, 183, 45, 188, 181, 0, 16, 58, 121, 232, 246, 127, 248, 203, 158, 42, 99, 25, 116, 224, 102, 138, 179, 151, 123, 245, 112, 169, 27, 103, 209, 182, 188, 213, 220, 232, 64, 85, 242, 20, 39, 214, 79, 66, 86, 160, 66, 171, 29, 200, 233, 37, 213, 58, 118, 159, 102, 129, 168, 115, 245, 133, 150, 147, 167, 114, 143, 203, 233, 91, 234, 206, 21, 99, 181, 255, 188, 215, 201, 59, 137, 138, 235, 163, 20, 33, 218, 251, 250, 222, 234, 80, 34, 156, 73, 253, 108, 68, 84, 73, 49, 68, 96, 243, 209, 145, 80, 189, 41, 169, 19, 51, 190, 172, 237, 85, 126, 214, 41, 82, 52, 247, 193, 79, 164, 99, 3, 183, 233, 119, 210, 200, 155, 168, 163, 154, 70, 163, 95, 51, 235, 7, 163, 50};
      std::string signature_str(signature.begin(), signature.end());
      static const std::string hashFunc = "sha256";
      {
          envoy::source::extensions::common::wasm::VerifySignatureArguments args;

          args.set_text(data);
          args.set_public_key(key_str);
          args.set_signature(signature_str);
          args.set_hash_function(hashFunc);

          std::string in;
          args.SerializeToString(&in);
          char* out = nullptr;
          size_t out_size = 0;

          if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                            in.size(), &out, &out_size)) {
              envoy::source::extensions::common::wasm::VerifySignatureResult result;
              if (result.ParseFromString(absl::string_view(out, out_size)) && result.result()) {
                  logInfo("signature is valid");
              } else {
                  logError(result.error());
              }
          }
          ::free(out);

      }
      {
          envoy::source::extensions::common::wasm::VerifySignatureArguments args;

          args.set_text(data.data());
          args.set_public_key(key_str.data());
          args.set_signature(signature_str.data());
          args.set_hash_function("unknown");

          std::string in;
          args.SerializeToString(&in);
          char* out = nullptr;
          size_t out_size = 0;
          if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                            in.size(), &out, &out_size)) {
              envoy::source::extensions::common::wasm::VerifySignatureResult result;
              if (result.ParseFromString(absl::string_view(out, out_size)) && result.result()) {
                  logCritical("signature should not be ok");
              } else {
                  logError(result.error());
              }
          }
          ::free(out);
      }
      {
          envoy::source::extensions::common::wasm::VerifySignatureArguments args;

          args.set_text(data.data());
          args.set_public_key(key_str.data());
          args.set_signature("0000");
          args.set_hash_function(hashFunc.data());

          std::string in;
          args.SerializeToString(&in);
          char* out = nullptr;
          size_t out_size = 0;
          if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                            in.size(), &out, &out_size)) {
              envoy::source::extensions::common::wasm::VerifySignatureResult result;
              if (result.ParseFromString(absl::string_view(out, out_size)) && result.result()) {
                  logCritical("signature should not be ok");
              } else {
                  logError(result.error());
              }
          }

          ::free(out);
      }
      {
          envoy::source::extensions::common::wasm::VerifySignatureArguments args;

          args.set_text("xxxx");
          args.set_public_key(key_str.data());
          args.set_signature(signature_str.data());
          args.set_hash_function(hashFunc.data());

          std::string in;
          args.SerializeToString(&in);
          char* out = nullptr;
          size_t out_size = 0;
          if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                            in.size(), &out, &out_size)) {
              envoy::source::extensions::common::wasm::VerifySignatureResult result;
              if (result.ParseFromString(absl::string_view(out, out_size)) && result.result()) {
                  logCritical("signature should not be ok");
              } else {
                  logError(result.error());
              }
          }

          ::free(out);
      }
  } else if (test_ == "sign") {
    std::string function = "sign";

    static const std::string data = "hello";
    // Proper PKCS#8 private key (DER format) - OpenSSL generated
    static const std::vector<uint8_t> private_key = {
        48,  130, 4,   190, 2,   1,   0,   48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,
        1,   1,   5,   0,   4,   130, 4,   168, 48,  130, 4,   164, 2,   1,   0,   2,   130, 1,
        1,   0,   214, 143, 48,  243, 96,  76,  62,  185, 171, 118, 5,   248, 75,  155, 218, 141,
        103, 191, 210, 241, 2,   18,  59,  12,  44,  113, 202, 255, 204, 13,  36,  163, 221, 94,
        41,  209, 124, 62,  178, 19,  168, 53,  227, 159, 73,  10,  129, 52,  112, 135, 41,  28,
        195, 229, 111, 30,  130, 21,  83,  119, 195, 167, 172, 187, 238, 210, 235, 85,  65,  146,
        73,  116, 99,  13,  101, 153, 69,  92,  126, 17,  35,  215, 53,  224, 106, 145, 90,  5,
        213, 240, 44,  97,  188, 199, 98,  46,  186, 188, 37,  79,  203, 116, 93,  123, 89,  246,
        232, 80,  116, 12,  145, 248, 178, 55,  51,  211, 213, 30,  122, 139, 180, 195, 190, 21,
        30,  66,  67,  241, 211, 195, 134, 204, 226, 110, 156, 49,  46,  194, 130, 6,   60,  3,
        139, 91,  2,   201, 7,   185, 171, 162, 97,  229, 179, 170, 93,  242, 171, 108, 249, 48,
        50,  182, 40,  229, 105, 215, 189, 213, 111, 179, 132, 16,  76,  176, 116, 143, 238, 191,
        42,  239, 77,  210, 169, 249, 34,  133, 102, 96,  26,  42,  127, 96,  80,  238, 71,  58,
        149, 122, 17,  76,  80,  19,  35,  222, 36,  163, 214, 5,   244, 3,   37,  245, 83,  172,
        141, 122, 114, 168, 86,  111, 180, 4,   63,  67,  16,  247, 102, 61,  12,  217, 187, 120,
        80,  154, 10,  250, 217, 146, 178, 27,  138, 53,  168, 175, 190, 20,  229, 143, 223, 203,
        32,  49,  244, 134, 20,  81,  2,   3,   1,   0,   1,   2,   130, 1,   1,   0,   209, 217,
        38,  197, 66,  112, 16,  1,   68,  131, 160, 127, 34,  100, 2,   179, 190, 191, 76,  174,
        207, 153, 201, 10,  181, 5,   110, 200, 20,  104, 222, 103, 46,  129, 132, 97,  17,  37,
        184, 193, 165, 9,   95,  225, 80,  108, 231, 197, 196, 49,  77,  178, 134, 158, 133, 185,
        206, 252, 208, 176, 24,  58,  140, 165, 26,  134, 76,  9,   12,  162, 233, 24,  222, 48,
        34,  40,  112, 64,  112, 68,  143, 75,  83,  180, 116, 70,  73,  71,  223, 224, 145, 103,
        222, 93,  27,  216, 28,  103, 28,  15,  25,  44,  108, 169, 210, 105, 188, 249, 195, 38,
        240, 53,  207, 8,   82,  182, 98,  128, 246, 214, 97,  43,  249, 99,  106, 62,  225, 119,
        126, 43,  32,  174, 43,  46,  51,  112, 51,  27,  253, 109, 64,  70,  72,  65,  206, 193,
        180, 178, 137, 220, 221, 7,   245, 119, 144, 81,  97,  150, 81,  84,  213, 251, 145, 64,
        8,   64,  214, 128, 73,  236, 32,  19,  6,   216, 198, 233, 108, 142, 115, 191, 29,  74,
        8,   157, 110, 221, 118, 194, 163, 52,  7,   202, 100, 152, 232, 150, 41,  71,  221, 20,
        174, 166, 229, 169, 39,  34,  107, 161, 85,  84,  231, 221, 185, 188, 6,   182, 72,  93,
        250, 146, 59,  131, 131, 16,  65,  76,  217, 50,  207, 52,  207, 62,  115, 214, 26,  32,
        222, 31,  147, 142, 246, 125, 124, 92,  216, 236, 36,  8,   185, 62,  50,  16,  212, 214,
        230, 129, 2,   129, 129, 0,   252, 165, 127, 10,  77,  249, 160, 60,  128, 237, 204, 94,
        69,  249, 112, 87,  86,  144, 228, 128, 207, 127, 85,  135, 155, 67,  47,  56,  236, 113,
        55,  83,  51,  68,  101, 231, 91,  221, 91,  175, 237, 95,  2,   146, 49,  251, 204, 248,
        250, 234, 74,  24,  17,  204, 170, 94,  175, 38,  93,  18,  116, 149, 12,  76,  5,   255,
        150, 62,  233, 167, 198, 192, 120, 239, 10,  105, 127, 4,   46,  100, 40,  242, 106, 19,
        206, 224, 220, 190, 72,  175, 236, 131, 157, 177, 51,  28,  55,  241, 207, 57,  147, 172,
        170, 63,  197, 122, 73,  220, 162, 216, 186, 104, 2,   61,  22,  164, 39,  227, 195, 14,
        30,  97,  117, 72,  255, 230, 243, 25,  2,   129, 129, 0,   217, 104, 69,  242, 154, 89,
        184, 47,  154, 68,  161, 207, 60,  246, 249, 2,   147, 9,   41,  232, 42,  76,  179, 113,
        198, 115, 81,  25,  229, 42,  137, 184, 123, 1,   108, 189, 248, 128, 226, 235, 187, 92,
        114, 24,  118, 218, 118, 51,  82,  186, 11,  209, 18,  94,  224, 244, 213, 151, 222, 237,
        131, 160, 7,   131, 168, 24,  181, 211, 208, 15,  213, 228, 4,   184, 141, 5,   193, 241,
        38,  165, 128, 242, 38,  230, 6,   130, 145, 140, 174, 42,  185, 151, 100, 126, 101, 16,
        133, 61,  224, 177, 177, 101, 62,  82,  160, 143, 157, 127, 38,  255, 42,  185, 154, 5,
        131, 231, 212, 11,  150, 165, 67,  151, 255, 219, 21,  205, 201, 249, 2,   129, 128, 123,
        14,  92,  231, 118, 253, 92,  55,  188, 16,  151, 87,  95,  187, 212, 37,  38,  43,  226,
        176, 126, 224, 165, 151, 44,  95,  183, 243, 128, 238, 208, 36,  189, 54,  214, 111, 175,
        6,   13,  111, 142, 45,  149, 194, 1,   136, 132, 216, 204, 214, 43,  10,  184, 56,  184,
        206, 239, 126, 191, 28,  139, 30,  65,  228, 17,  147, 224, 233, 121, 195, 87,  130, 78,
        37,  24,  44,  52,  74,  164, 17,  243, 3,   199, 249, 39,  237, 204, 118, 254, 78,  121,
        227, 205, 126, 14,  199, 242, 211, 219, 188, 78,  154, 110, 62,  43,  128, 153, 211, 86,
        154, 59,  137, 7,   118, 27,  190, 15,  19,  215, 224, 219, 153, 90,  152, 24,  212, 179,
        153, 2,   129, 128, 46,  10,  134, 29,  173, 144, 104, 144, 52,  52,  106, 172, 15,  182,
        33,  223, 232, 177, 157, 29,  92,  175, 231, 164, 165, 169, 80,  56,  146, 174, 162, 129,
        222, 18,  220, 43,  147, 16,  0,   126, 121, 172, 71,  65,  101, 18,  56,  203, 255, 174,
        4,   200, 159, 2,   86,  211, 162, 212, 73,  210, 180, 248, 83,  255, 14,  191, 68,  234,
        121, 122, 145, 10,  123, 241, 117, 116, 13,  177, 123, 68,  187, 125, 12,  189, 212, 13,
        28,  179, 213, 66,  153, 17,  53,  168, 10,  209, 39,  214, 133, 218, 59,  190, 60,  39,
        149, 119, 52,  156, 115, 238, 191, 230, 148, 118, 172, 139, 144, 253, 111, 70,  202, 124,
        203, 35,  99,  135, 180, 73,  2,   129, 129, 0,   131, 123, 236, 100, 40,  60,  98,  188,
        174, 125, 210, 178, 176, 170, 220, 199, 152, 95,  221, 32,  2,   149, 91,  234, 121, 237,
        138, 24,  193, 85,  146, 33,  51,  219, 188, 109, 185, 56,  63,  196, 24,  100, 58,  197,
        93,  96,  96,  168, 237, 146, 18,  198, 62,  50,  254, 252, 47,  230, 139, 107, 74,  96,
        5,   124, 190, 162, 84,  112, 14,  24,  165, 161, 226, 183, 35,  198, 127, 254, 82,  234,
        108, 10,  184, 124, 35,  66,  113, 238, 239, 162, 173, 73,  97,  158, 0,   13,  209, 112,
        51,  167, 174, 94,  110, 222, 99,  174, 52,  64,  122, 21,  194, 219, 93,  219, 202, 101,
        93,  210, 37,  3,   214, 113, 240, 187, 185, 93,  226, 126};
    std::string private_key_str(private_key.begin(), private_key.end());
    static const std::string hashFunc = "sha256";

    {
      envoy::source::extensions::common::wasm::SignArguments args;

      args.set_text(data);
      args.set_private_key(private_key_str);
      args.set_hash_function(hashFunc);

      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;

      if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        envoy::source::extensions::common::wasm::SignResult result;
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
          logInfo("signature created successfully");
        } else {
          logError(result.error());
        }
      }
      ::free(out);
    }

    {
      envoy::source::extensions::common::wasm::SignArguments args;

      args.set_text(data);
      args.set_private_key(private_key_str);
      args.set_hash_function("unknown");

      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        envoy::source::extensions::common::wasm::SignResult result;
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
          logCritical("signature should not be ok");
        } else {
          logError(result.error());
        }
      }
      ::free(out);
    }

    {
      envoy::source::extensions::common::wasm::SignArguments args;

      args.set_text("xxxx");
      args.set_private_key("0000");
      args.set_hash_function(hashFunc);

      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;
      if (WasmResult::Ok == proxy_call_foreign_function(function.data(), function.size(), in.data(),
                                                        in.size(), &out, &out_size)) {
        envoy::source::extensions::common::wasm::SignResult result;
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
          logCritical("signature should not be ok");
        } else {
          logError(result.error());
        }
      }
      ::free(out);
    }
  } else if (test_ == "sign_and_verify_signature") {
    std::string sign_function = "sign";
    std::string verify_function = "verify_signature";

    static const std::string data = "hello world";
    // Proper PKCS#8 private key (DER format) - OpenSSL generated
    static const std::vector<uint8_t> private_key = {
        48,  130, 4,   190, 2,   1,   0,   48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,
        1,   1,   5,   0,   4,   130, 4,   168, 48,  130, 4,   164, 2,   1,   0,   2,   130, 1,
        1,   0,   214, 143, 48,  243, 96,  76,  62,  185, 171, 118, 5,   248, 75,  155, 218, 141,
        103, 191, 210, 241, 2,   18,  59,  12,  44,  113, 202, 255, 204, 13,  36,  163, 221, 94,
        41,  209, 124, 62,  178, 19,  168, 53,  227, 159, 73,  10,  129, 52,  112, 135, 41,  28,
        195, 229, 111, 30,  130, 21,  83,  119, 195, 167, 172, 187, 238, 210, 235, 85,  65,  146,
        73,  116, 99,  13,  101, 153, 69,  92,  126, 17,  35,  215, 53,  224, 106, 145, 90,  5,
        213, 240, 44,  97,  188, 199, 98,  46,  186, 188, 37,  79,  203, 116, 93,  123, 89,  246,
        232, 80,  116, 12,  145, 248, 178, 55,  51,  211, 213, 30,  122, 139, 180, 195, 190, 21,
        30,  66,  67,  241, 211, 195, 134, 204, 226, 110, 156, 49,  46,  194, 130, 6,   60,  3,
        139, 91,  2,   201, 7,   185, 171, 162, 97,  229, 179, 170, 93,  242, 171, 108, 249, 48,
        50,  182, 40,  229, 105, 215, 189, 213, 111, 179, 132, 16,  76,  176, 116, 143, 238, 191,
        42,  239, 77,  210, 169, 249, 34,  133, 102, 96,  26,  42,  127, 96,  80,  238, 71,  58,
        149, 122, 17,  76,  80,  19,  35,  222, 36,  163, 214, 5,   244, 3,   37,  245, 83,  172,
        141, 122, 114, 168, 86,  111, 180, 4,   63,  67,  16,  247, 102, 61,  12,  217, 187, 120,
        80,  154, 10,  250, 217, 146, 178, 27,  138, 53,  168, 175, 190, 20,  229, 143, 223, 203,
        32,  49,  244, 134, 20,  81,  2,   3,   1,   0,   1,   2,   130, 1,   1,   0,   209, 217,
        38,  197, 66,  112, 16,  1,   68,  131, 160, 127, 34,  100, 2,   179, 190, 191, 76,  174,
        207, 153, 201, 10,  181, 5,   110, 200, 20,  104, 222, 103, 46,  129, 132, 97,  17,  37,
        184, 193, 165, 9,   95,  225, 80,  108, 231, 197, 196, 49,  77,  178, 134, 158, 133, 185,
        206, 252, 208, 176, 24,  58,  140, 165, 26,  134, 76,  9,   12,  162, 233, 24,  222, 48,
        34,  40,  112, 64,  112, 68,  143, 75,  83,  180, 116, 70,  73,  71,  223, 224, 145, 103,
        222, 93,  27,  216, 28,  103, 28,  15,  25,  44,  108, 169, 210, 105, 188, 249, 195, 38,
        240, 53,  207, 8,   82,  182, 98,  128, 246, 214, 97,  43,  249, 99,  106, 62,  225, 119,
        126, 43,  32,  174, 43,  46,  51,  112, 51,  27,  253, 109, 64,  70,  72,  65,  206, 193,
        180, 178, 137, 220, 221, 7,   245, 119, 144, 81,  97,  150, 81,  84,  213, 251, 145, 64,
        8,   64,  214, 128, 73,  236, 32,  19,  6,   216, 198, 233, 108, 142, 115, 191, 29,  74,
        8,   157, 110, 221, 118, 194, 163, 52,  7,   202, 100, 152, 232, 150, 41,  71,  221, 20,
        174, 166, 229, 169, 39,  34,  107, 161, 85,  84,  231, 221, 185, 188, 6,   182, 72,  93,
        250, 146, 59,  131, 131, 16,  65,  76,  217, 50,  207, 52,  207, 62,  115, 214, 26,  32,
        222, 31,  147, 142, 246, 125, 124, 92,  216, 236, 36,  8,   185, 62,  50,  16,  212, 214,
        230, 129, 2,   129, 129, 0,   252, 165, 127, 10,  77,  249, 160, 60,  128, 237, 204, 94,
        69,  249, 112, 87,  86,  144, 228, 128, 207, 127, 85,  135, 155, 67,  47,  56,  236, 113,
        55,  83,  51,  68,  101, 231, 91,  221, 91,  175, 237, 95,  2,   146, 49,  251, 204, 248,
        250, 234, 74,  24,  17,  204, 170, 94,  175, 38,  93,  18,  116, 149, 12,  76,  5,   255,
        150, 62,  233, 167, 198, 192, 120, 239, 10,  105, 127, 4,   46,  100, 40,  242, 106, 19,
        206, 224, 220, 190, 72,  175, 236, 131, 157, 177, 51,  28,  55,  241, 207, 57,  147, 172,
        170, 63,  197, 122, 73,  220, 162, 216, 186, 104, 2,   61,  22,  164, 39,  227, 195, 14,
        30,  97,  117, 72,  255, 230, 243, 25,  2,   129, 129, 0,   217, 104, 69,  242, 154, 89,
        184, 47,  154, 68,  161, 207, 60,  246, 249, 2,   147, 9,   41,  232, 42,  76,  179, 113,
        198, 115, 81,  25,  229, 42,  137, 184, 123, 1,   108, 189, 248, 128, 226, 235, 187, 92,
        114, 24,  118, 218, 118, 51,  82,  186, 11,  209, 18,  94,  224, 244, 213, 151, 222, 237,
        131, 160, 7,   131, 168, 24,  181, 211, 208, 15,  213, 228, 4,   184, 141, 5,   193, 241,
        38,  165, 128, 242, 38,  230, 6,   130, 145, 140, 174, 42,  185, 151, 100, 126, 101, 16,
        133, 61,  224, 177, 177, 101, 62,  82,  160, 143, 157, 127, 38,  255, 42,  185, 154, 5,
        131, 231, 212, 11,  150, 165, 67,  151, 255, 219, 21,  205, 201, 249, 2,   129, 128, 123,
        14,  92,  231, 118, 253, 92,  55,  188, 16,  151, 87,  95,  187, 212, 37,  38,  43,  226,
        176, 126, 224, 165, 151, 44,  95,  183, 243, 128, 238, 208, 36,  189, 54,  214, 111, 175,
        6,   13,  111, 142, 45,  149, 194, 1,   136, 132, 216, 204, 214, 43,  10,  184, 56,  184,
        206, 239, 126, 191, 28,  139, 30,  65,  228, 17,  147, 224, 233, 121, 195, 87,  130, 78,
        37,  24,  44,  52,  74,  164, 17,  243, 3,   199, 249, 39,  237, 204, 118, 254, 78,  121,
        227, 205, 126, 14,  199, 242, 211, 219, 188, 78,  154, 110, 62,  43,  128, 153, 211, 86,
        154, 59,  137, 7,   118, 27,  190, 15,  19,  215, 224, 219, 153, 90,  152, 24,  212, 179,
        153, 2,   129, 128, 46,  10,  134, 29,  173, 144, 104, 144, 52,  52,  106, 172, 15,  182,
        33,  223, 232, 177, 157, 29,  92,  175, 231, 164, 165, 169, 80,  56,  146, 174, 162, 129,
        222, 18,  220, 43,  147, 16,  0,   126, 121, 172, 71,  65,  101, 18,  56,  203, 255, 174,
        4,   200, 159, 2,   86,  211, 162, 212, 73,  210, 180, 248, 83,  255, 14,  191, 68,  234,
        121, 122, 145, 10,  123, 241, 117, 116, 13,  177, 123, 68,  187, 125, 12,  189, 212, 13,
        28,  179, 213, 66,  153, 17,  53,  168, 10,  209, 39,  214, 133, 218, 59,  190, 60,  39,
        149, 119, 52,  156, 115, 238, 191, 230, 148, 118, 172, 139, 144, 253, 111, 70,  202, 124,
        203, 35,  99,  135, 180, 73,  2,   129, 129, 0,   131, 123, 236, 100, 40,  60,  98,  188,
        174, 125, 210, 178, 176, 170, 220, 199, 152, 95,  221, 32,  2,   149, 91,  234, 121, 237,
        138, 24,  193, 85,  146, 33,  51,  219, 188, 109, 185, 56,  63,  196, 24,  100, 58,  197,
        93,  96,  96,  168, 237, 146, 18,  198, 62,  50,  254, 252, 47,  230, 139, 107, 74,  96,
        5,   124, 190, 162, 84,  112, 14,  24,  165, 161, 226, 183, 35,  198, 127, 254, 82,  234,
        108, 10,  184, 124, 35,  66,  113, 238, 239, 162, 173, 73,  97,  158, 0,   13,  209, 112,
        51,  167, 174, 94,  110, 222, 99,  174, 52,  64,  122, 21,  194, 219, 93,  219, 202, 101,
        93,  210, 37,  3,   214, 113, 240, 187, 185, 93,  226, 126};
    // Corresponding PKCS#1 public key (DER format) - OpenSSL generated
    static const std::vector<uint8_t> public_key = {
        48,  130, 1,   34,  48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,   1,   1,   5,
        0,   3,   130, 1,   15,  0,   48,  130, 1,   10,  2,   130, 1,   1,   0,   214, 143, 48,
        243, 96,  76,  62,  185, 171, 118, 5,   248, 75,  155, 218, 141, 103, 191, 210, 241, 2,
        18,  59,  12,  44,  113, 202, 255, 204, 13,  36,  163, 221, 94,  41,  209, 124, 62,  178,
        19,  168, 53,  227, 159, 73,  10,  129, 52,  112, 135, 41,  28,  195, 229, 111, 30,  130,
        21,  83,  119, 195, 167, 172, 187, 238, 210, 235, 85,  65,  146, 73,  116, 99,  13,  101,
        153, 69,  92,  126, 17,  35,  215, 53,  224, 106, 145, 90,  5,   213, 240, 44,  97,  188,
        199, 98,  46,  186, 188, 37,  79,  203, 116, 93,  123, 89,  246, 232, 80,  116, 12,  145,
        248, 178, 55,  51,  211, 213, 30,  122, 139, 180, 195, 190, 21,  30,  66,  67,  241, 211,
        195, 134, 204, 226, 110, 156, 49,  46,  194, 130, 6,   60,  3,   139, 91,  2,   201, 7,
        185, 171, 162, 97,  229, 179, 170, 93,  242, 171, 108, 249, 48,  50,  182, 40,  229, 105,
        215, 189, 213, 111, 179, 132, 16,  76,  176, 116, 143, 238, 191, 42,  239, 77,  210, 169,
        249, 34,  133, 102, 96,  26,  42,  127, 96,  80,  238, 71,  58,  149, 122, 17,  76,  80,
        19,  35,  222, 36,  163, 214, 5,   244, 3,   37,  245, 83,  172, 141, 122, 114, 168, 86,
        111, 180, 4,   63,  67,  16,  247, 102, 61,  12,  217, 187, 120, 80,  154, 10,  250, 217,
        146, 178, 27,  138, 53,  168, 175, 190, 20,  229, 143, 223, 203, 32,  49,  244, 134, 20,
        81,  2,   3,   1,   0,   1};
    std::string private_key_str(private_key.begin(), private_key.end());
    std::string public_key_str(public_key.begin(), public_key.end());
    static const std::string hashFunc = "sha256";

    // Step 1: Create a signature using sign
    {
      envoy::source::extensions::common::wasm::SignArguments sign_args;
      sign_args.set_text(data);
      sign_args.set_private_key(private_key_str);
      sign_args.set_hash_function(hashFunc);

      std::string sign_in;
      sign_args.SerializeToString(&sign_in);
      char* sign_out = nullptr;
      size_t sign_out_size = 0;

      if (WasmResult::Ok == proxy_call_foreign_function(sign_function.data(), sign_function.size(),
                                                        sign_in.data(), sign_in.size(), &sign_out,
                                                        &sign_out_size)) {
        envoy::source::extensions::common::wasm::SignResult sign_result;
        if (sign_result.ParseFromArray(sign_out, static_cast<int>(sign_out_size)) &&
            sign_result.result()) {
          logInfo("signature created successfully, length: " +
                  std::to_string(sign_result.signature().size()));

          // Step 2: Verify the signature using verify_signature
          {
            envoy::source::extensions::common::wasm::VerifySignatureArguments verify_args;
            verify_args.set_text(data);
            verify_args.set_public_key(public_key_str);
            verify_args.set_signature(sign_result.signature());
            verify_args.set_hash_function(hashFunc);

            std::string verify_in;
            verify_args.SerializeToString(&verify_in);
            char* verify_out = nullptr;
            size_t verify_out_size = 0;

            if (WasmResult::Ok == proxy_call_foreign_function(verify_function.data(),
                                                              verify_function.size(),
                                                              verify_in.data(), verify_in.size(),
                                                              &verify_out, &verify_out_size)) {
              envoy::source::extensions::common::wasm::VerifySignatureResult verify_result;
              if (verify_result.ParseFromArray(verify_out, static_cast<int>(verify_out_size)) &&
                  verify_result.result()) {
                logInfo("end-to-end test passed: signature created and verified successfully");
              } else {
                logError("end-to-end test failed: signature verification failed: " +
                          verify_result.error());
              }
            } else {
              logError("end-to-end test failed: verify_signature call failed");
            }
            ::free(verify_out);
          }
        } else {
          logError("end-to-end test failed: signature creation failed: " + sign_result.error());
        }
      } else {
        logError("end-to-end test failed: sign call failed");
      }
      ::free(sign_out);
    }

    // Test mutual exclusion: should fail if both PEM and DER keys are provided
    {
      envoy::source::extensions::common::wasm::VerifySignatureArguments args;
      args.set_text(data);
      args.set_public_key(public_key_str);      // DER key
      args.set_public_key_pem("dummy_pem_key"); // PEM key (dummy, but valid UTF-8)
      args.set_signature("dummy_signature");    // Dummy signature for test
      args.set_hash_function(hashFunc);

      std::string in;
      args.SerializeToString(&in);
      char* out = nullptr;
      size_t out_size = 0;

      if (WasmResult::BadArgument == proxy_call_foreign_function(verify_function.data(),
                                                                  verify_function.size(), in.data(),
                                                                  in.size(), &out, &out_size)) {
        logInfo("mutual exclusion test passed: both PEM and DER keys rejected");
      } else {
        logError("mutual exclusion test failed: should have rejected both PEM and DER keys");
      }
      ::free(out);
    }
  }
}

class Context1 : public Context {
public:
  Context1(uint32_t id, RootContext* root) : Context(id, root) {}
  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

class Context2 : public Context {
public:
  Context2(uint32_t id, RootContext* root) : Context(id, root) {}
  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

static RegisterContextFactory register_Context1(CONTEXT_FACTORY(Context1), "context1");
static RegisterContextFactory register_Contxt2(CONTEXT_FACTORY(Context2), "context2");

FilterHeadersStatus Context1::onRequestHeaders(uint32_t, bool) {
  logDebug(std::string("onRequestHeaders1 ") + std::to_string(id()));
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus Context2::onRequestHeaders(uint32_t, bool) {
  logDebug(std::string("onRequestHeaders2 ") + std::to_string(id()));
  CHECK_RESULT(sendLocalResponse(200, "ok", "body", {{"foo", "bar"}}));
  return FilterHeadersStatus::Continue;
}

END_WASM_PLUGIN
