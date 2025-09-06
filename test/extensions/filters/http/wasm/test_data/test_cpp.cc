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
          "string_state",      "metadata",
          "request",           "response",
          "connection",        "connection_id",
          "upstream",          "source",
          "destination",       "cluster_name",
          "cluster_metadata",  "route_name",
          "route_metadata",    "upstream_host_metadata",
          "filter_state",      "listener_direction",
          "listener_metadata",
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
    if (getValue({"xds", "cluster_metadata", "filter_metadata", "namespace", "key"},
                 &cluster_metadata)) {
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
      if (getValue({"xds", "upstream_host_metadata", "filter_metadata", "namespace", "key"},
                   &upstream_host_metadata)) {
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
    if (!getValue({"xds", "node", "metadata", "wasm_node_get_key"},
                  &value)) { // NOLINT(clang-analyzer-optin.portability.UnixAPI)
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
    static const std::vector<uint8_t> key = {
        48,  130, 1,   34,  48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,   1,   1,   5,
        0,   3,   130, 1,   15,  0,   48,  130, 1,   10,  2,   130, 1,   1,   0,   167, 71,  18,
        102, 208, 29,  22,  3,   8,   215, 52,  9,   192, 111, 46,  141, 53,  197, 49,  196, 88,
        211, 228, 128, 233, 243, 25,  24,  71,  208, 98,  236, 92,  207, 247, 188, 81,  233, 73,
        213, 242, 195, 84,  12,  24,  154, 78,  202, 30,  134, 51,  166, 44,  242, 208, 146, 49,
        1,   194, 126, 56,  1,   62,  113, 222, 154, 233, 26,  112, 72,  73,  191, 247, 251, 226,
        206, 91,  244, 189, 102, 111, 217, 115, 17,  2,   165, 49,  147, 254, 90,  154, 90,  80,
        100, 79,  248, 177, 24,  63,  168, 151, 100, 101, 152, 202, 173, 34,  163, 127, 149, 68,
        81,  8,   54,  55,  43,  68,  197, 140, 152, 88,  111, 183, 20,  70,  41,  205, 140, 148,
        121, 89,  45,  153, 109, 50,  255, 109, 57,  92,  11,  132, 66,  236, 90,  161, 239, 128,
        81,  82,  158, 160, 227, 117, 136, 60,  239, 199, 44,  4,   227, 96,  180, 239, 143, 87,
        96,  101, 5,   137, 202, 129, 73,  24,  246, 120, 238, 227, 155, 136, 77,  90,  248, 19,
        106, 150, 48,  166, 204, 12,  222, 21,  125, 200, 224, 15,  57,  84,  6,   40,  213, 243,
        53,  178, 195, 108, 84,  199, 200, 188, 55,  56,  166, 178, 26,  207, 248, 21,  64,  90,
        250, 40,  229, 24,  63,  85,  13,  172, 25,  171, 207, 17,  69,  167, 249, 206, 217, 135,
        219, 104, 14,  74,  34,  156, 172, 117, 222, 227, 71,  236, 158, 188, 225, 252, 61,  187,
        187, 2,   3,   1,   0,   1};
    std::string key_str(key.begin(), key.end());
    static const std::vector<uint8_t> signature = {
        52,  90,  195, 161, 103, 85,  143, 79,  56,  122, 129, 194, 214, 66,  52,  217, 1,   167,
        206, 170, 84,  77,  183, 121, 210, 247, 151, 176, 234, 78,  248, 81,  183, 64,  144, 90,
        99,  226, 244, 213, 175, 66,  206, 224, 147, 162, 156, 113, 85,  219, 154, 99,  211, 212,
        131, 224, 239, 148, 143, 90,  197, 28,  228, 225, 10,  58,  102, 6,   253, 147, 239, 104,
        238, 71,  179, 12,  55,  73,  17,  3,   3,   148, 89,  18,  47,  120, 225, 199, 234, 113,
        161, 165, 234, 36,  187, 101, 25,  188, 160, 44,  140, 153, 21,  254, 139, 226, 73,  39,
        201, 24,  18,  161, 61,  183, 45,  188, 181, 0,   16,  58,  121, 232, 246, 127, 248, 203,
        158, 42,  99,  25,  116, 224, 102, 138, 179, 151, 123, 245, 112, 169, 27,  103, 209, 182,
        188, 213, 220, 232, 64,  85,  242, 20,  39,  214, 79,  66,  86,  160, 66,  171, 29,  200,
        233, 37,  213, 58,  118, 159, 102, 129, 168, 115, 245, 133, 150, 147, 167, 114, 143, 203,
        233, 91,  234, 206, 21,  99,  181, 255, 188, 215, 201, 59,  137, 138, 235, 163, 20,  33,
        218, 251, 250, 222, 234, 80,  34,  156, 73,  253, 108, 68,  84,  73,  49,  68,  96,  243,
        209, 145, 80,  189, 41,  169, 19,  51,  190, 172, 237, 85,  126, 214, 41,  82,  52,  247,
        193, 79,  164, 99,  3,   183, 233, 119, 210, 200, 155, 168, 163, 154, 70,  163, 95,  51,
        235, 7,   163, 50};
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
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
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
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
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
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
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
        if (result.ParseFromArray(out, static_cast<int>(out_size)) && result.result()) {
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
    // Proper PKCS#8 private key (DER format) - same as used in other tests
    static const std::vector<uint8_t> private_key = {
        48,  130, 4,   190, 2,   1,   0,   48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,
        1,   1,   5,   0,   4,   130, 4,   168, 48,  130, 4,   164, 2,   1,   0,   2,   130, 1,
        1,   0,   206, 121, 1,   194, 150, 84,  247, 224, 78,  8,   2,   207, 100, 16,  201, 227,
        84,  206, 11,  202, 175, 166, 222, 37,  33,  228, 83,  240, 243, 248, 192, 118, 7,   56,
        155, 188, 106, 171, 162, 46,  65,  191, 245, 18,  68,  208, 167, 184, 125, 29,  39,  29,
        39,  218, 152, 209, 107, 50,  77,  10,  206, 128, 188, 156, 35,  108, 51,  194, 74,  150,
        231, 0,   155, 78,  46,  97,  141, 36,  73,  19,  4,   21,  228, 0,   28,  192, 142, 93,
        172, 167, 181, 121, 78,  214, 31,  238, 29,  181, 191, 135, 154, 41,  236, 224, 236, 42,
        249, 39,  129, 158, 90,  92,  55,  228, 92,  15,  195, 174, 19,  173, 243, 153, 40,  40,
        228, 217, 125, 125, 123, 91,  253, 122, 6,   49,  129, 47,  43,  173, 209, 186, 108, 111,
        136, 207, 215, 103, 229, 61,  100, 244, 122, 196, 246, 21,  37,  228, 53,  219, 98,  99,
        86,  87,  15,  30,  2,   255, 12,  228, 215, 187, 146, 189, 134, 94,  223, 208, 243, 151,
        138, 124, 204, 5,   156, 3,   74,  96,  101, 207, 145, 120, 33,  218, 224, 185, 167, 33,
        223, 24,  139, 116, 65,  81,  206, 140, 194, 137, 98,  91,  129, 134, 246, 138, 186, 82,
        144, 184, 213, 104, 109, 139, 127, 102, 35,  19,  40,  219, 154, 66,  213, 192, 60,  36,
        104, 90,  9,   34,  170, 158, 52,  217, 94,  100, 62,  17,  85,  85,  152, 214, 32,  204,
        31,  113, 133, 165, 212, 23,  2,   3,   1,   0,   1,   2,   130, 1,   0,   77,  92,  241,
        215, 227, 84,  58,  252, 132, 192, 99,  173, 41,  165, 80,  192, 41,  74,  123, 8,   155,
        0,   63,  68,  82,  138, 167, 25,  37,  145, 19,  44,  38,  80,  131, 169, 249, 158, 13,
        202, 159, 64,  57,  167, 122, 185, 99,  222, 176, 162, 119, 193, 104, 233, 115, 81,  36,
        133, 88,  112, 176, 39,  116, 132, 92,  145, 114, 99,  94,  103, 100, 110, 201, 194, 101,
        134, 143, 200, 4,   201, 103, 66,  124, 135, 190, 62,  56,  25,  201, 83,  157, 159, 178,
        118, 112, 200, 91,  193, 121, 222, 105, 89,  68,  52,  146, 201, 23,  74,  66,  58,  255,
        72,  134, 120, 190, 53,  249, 240, 3,   215, 173, 234, 185, 45,  121, 114, 52,  158, 95,
        90,  77,  33,  236, 201, 238, 203, 129, 33,  50,  223, 206, 196, 71,  116, 84,  224, 156,
        7,   245, 22,  132, 223, 71,  32,  224, 74,  158, 36,  54,  45,  184, 205, 33,  150, 193,
        128, 71,  130, 166, 130, 23,  75,  77,  201, 119, 168, 78,  178, 124, 31,  102, 79,  34,
        235, 100, 179, 171, 174, 67,  61,  4,   95,  180, 238, 163, 115, 11,  196, 239, 48,  208,
        251, 133, 152, 71,  29,  234, 44,  120, 246, 84,  235, 205, 237, 139, 116, 54,  21,  92,
        31,  3,   54,  46,  132, 9,   192, 99,  96,  34,  184, 17,  107, 206, 212, 196, 96,  153,
        197, 63,  164, 216, 216, 209, 244, 246, 190, 119, 117, 253, 68,  142, 168, 136, 68,  77,
        161, 2,   129, 129, 0,   241, 31,  184, 143, 133, 20,  32,  45,  78,  59,  19,  114, 112,
        243, 203, 152, 216, 225, 127, 201, 202, 247, 124, 118, 237, 169, 161, 188, 14,  44,  235,
        196, 195, 153, 122, 191, 150, 188, 219, 148, 91,  238, 222, 62,  1,   214, 70,  73,  19,
        244, 70,  213, 148, 33,  134, 119, 97,  158, 205, 181, 132, 182, 61,  202, 129, 202, 205,
        159, 169, 3,   10,  0,   213, 187, 20,  52,  131, 184, 170, 168, 106, 125, 134, 22,  173,
        193, 102, 69,  55,  108, 137, 4,   226, 89,  231, 132, 229, 252, 237, 55,  19,  94,  168,
        247, 118, 148, 12,  211, 55,  21,  80,  172, 220, 26,  242, 212, 9,   191, 193, 173, 114,
        83,  171, 21,  65,  84,  15,  221, 2,   129, 129, 0,   219, 54,  2,   81,  92,  22,  11,
        65,  128, 61,  115, 42,  251, 187, 143, 65,  31,  192, 36,  100, 137, 50,  228, 78,  125,
        216, 231, 40,  203, 254, 123, 197, 40,  42,  111, 87,  2,   121, 100, 200, 186, 34,  97,
        138, 131, 241, 22,  29,  24,  114, 81,  239, 213, 222, 59,  183, 200, 61,  80,  219, 98,
        149, 177, 57,  46,  158, 135, 194, 5,   118, 24,  88,  218, 237, 5,   115, 23,  216, 21,
        202, 254, 82,  37,  62,  175, 47,  114, 104, 151, 150, 94,  212, 111, 10,  33,  45,  131,
        85,  162, 210, 230, 72,  130, 233, 227, 33,  102, 204, 167, 228, 51,  108, 195, 178, 121,
        172, 224, 246, 122, 190, 225, 38,  227, 144, 135, 104, 46,  131, 2,   129, 129, 0,   236,
        9,   27,  72,  19,  3,   162, 131, 247, 34,  201, 100, 171, 193, 91,  186, 98,  4,   76,
        109, 163, 44,  37,  64,  222, 97,  193, 155, 47,  93,  53,  230, 197, 122, 198, 184, 41,
        188, 242, 78,  6,   184, 140, 1,   179, 22,  168, 114, 252, 255, 145, 31,  158, 4,   59,
        119, 61,  174, 144, 188, 114, 15,  91,  233, 146, 168, 142, 37,  14,  243, 148, 165, 64,
        148, 3,   177, 108, 136, 39,  54,  250, 23,  170, 93,  36,  246, 63,  64,  222, 130, 118,
        150, 187, 101, 58,  199, 211, 195, 134, 10,  246, 1,   33,  242, 44,  183, 188, 222, 61,
        251, 181, 159, 161, 79,  24,  10,  13,  9,   19,  116, 208, 135, 170, 224, 1,   181, 98,
        89,  2,   129, 129, 0,   17,  86,  25,  34,  212, 20,  142, 57,  84,  234, 7,   52,  172,
        9,   238, 79,  105, 50,  105, 238, 101, 135, 87,  212, 249, 80,  241, 31,  33,  218, 243,
        112, 233, 55,  73,  236, 232, 174, 47,  17,  76,  223, 49,  53,  162, 47,  171, 223, 11,
        50,  231, 85,  255, 100, 254, 246, 14,  233, 2,   127, 7,   49,  237, 125, 39,  57,  180,
        100, 220, 199, 181, 47,  57,  201, 42,  248, 42,  55,  149, 169, 163, 41,  93,  246, 178,
        38,  31,  119, 52,  29,  217, 76,  21,  168, 8,   109, 176, 8,   82,  195, 57,  33,  28,
        241, 96,  92,  32,  228, 40,  150, 252, 150, 42,  119, 239, 245, 131, 41,  27,  22,  3,
        122, 110, 222, 220, 70,  153, 255, 2,   129, 129, 0,   202, 220, 12,  189, 78,  79,  0,
        48,  30,  53,  148, 25,  5,   41,  200, 50,  76,  25,  237, 119, 19,  139, 117, 130, 40,
        138, 34,  159, 134, 198, 242, 97,  249, 91,  147, 212, 122, 49,  136, 86,  179, 88,  94,
        104, 177, 185, 11,  230, 200, 70,  122, 78,  143, 151, 246, 232, 32,  6,   79,  141, 39,
        147, 221, 249, 62,  28,  250, 17,  159, 31,  22,  109, 225, 93,  101, 136, 217, 232, 172,
        95,  253, 48,  201, 83,  55,  76,  34,  85,  125, 63,  128, 210, 73,  130, 66,  93,  254,
        0,   117, 76,  250, 184, 16,  200, 255, 18,  106, 223, 176, 153, 100, 211, 96,  209, 210,
        211, 55,  207, 48,  118, 197, 62,  77,  89,  249, 17,  254, 238};
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
    // Proper PKCS#8 private key (DER format)
    static const std::vector<uint8_t> private_key = {
        48,  130, 4,   190, 2,   1,   0,   48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,
        1,   1,   5,   0,   4,   130, 4,   168, 48,  130, 4,   164, 2,   1,   0,   2,   130, 1,
        1,   0,   206, 121, 1,   194, 150, 84,  247, 224, 78,  8,   2,   207, 100, 16,  201, 227,
        84,  206, 11,  202, 175, 166, 222, 37,  33,  228, 83,  240, 243, 248, 192, 118, 7,   56,
        155, 188, 106, 171, 162, 46,  65,  191, 245, 18,  68,  208, 167, 184, 125, 29,  39,  29,
        39,  218, 152, 209, 107, 50,  77,  10,  206, 128, 188, 156, 35,  108, 51,  194, 74,  150,
        231, 0,   155, 78,  46,  97,  141, 36,  73,  19,  4,   21,  228, 0,   28,  192, 142, 93,
        172, 167, 181, 121, 78,  214, 31,  238, 29,  181, 191, 135, 154, 41,  236, 224, 236, 42,
        249, 39,  129, 158, 90,  92,  55,  228, 92,  15,  195, 174, 19,  173, 243, 153, 40,  40,
        228, 217, 125, 125, 123, 91,  253, 122, 6,   49,  129, 47,  43,  173, 209, 186, 108, 111,
        136, 207, 215, 103, 229, 61,  100, 244, 122, 196, 246, 21,  37,  228, 53,  219, 98,  99,
        86,  87,  15,  30,  2,   255, 12,  228, 215, 187, 146, 189, 134, 94,  223, 208, 243, 151,
        138, 124, 204, 5,   156, 3,   74,  96,  101, 207, 145, 120, 33,  218, 224, 185, 167, 33,
        223, 24,  139, 116, 65,  81,  206, 140, 194, 137, 98,  91,  129, 134, 246, 138, 186, 82,
        144, 184, 213, 104, 109, 139, 127, 102, 35,  19,  40,  219, 154, 66,  213, 192, 60,  36,
        104, 90,  9,   34,  170, 158, 52,  217, 94,  100, 62,  17,  85,  85,  152, 214, 32,  204,
        31,  113, 133, 165, 212, 23,  2,   3,   1,   0,   1,   2,   130, 1,   0,   77,  92,  241,
        215, 227, 84,  58,  252, 132, 192, 99,  173, 41,  165, 80,  192, 41,  74,  123, 8,   155,
        0,   63,  68,  82,  138, 167, 25,  37,  145, 19,  44,  38,  80,  131, 169, 249, 158, 13,
        202, 159, 64,  57,  167, 122, 185, 99,  222, 176, 162, 119, 193, 104, 233, 115, 81,  36,
        133, 88,  112, 176, 39,  116, 132, 92,  145, 114, 99,  94,  103, 100, 110, 201, 194, 101,
        134, 143, 200, 4,   201, 103, 66,  124, 135, 190, 62,  56,  25,  201, 83,  157, 159, 178,
        118, 112, 200, 91,  193, 121, 222, 105, 89,  68,  52,  146, 201, 23,  74,  66,  58,  255,
        72,  134, 120, 190, 53,  249, 240, 3,   215, 173, 234, 185, 45,  121, 114, 52,  158, 95,
        90,  77,  33,  236, 201, 238, 203, 129, 33,  50,  223, 206, 196, 71,  116, 84,  224, 156,
        7,   245, 22,  132, 223, 71,  32,  224, 74,  158, 36,  54,  45,  184, 205, 33,  150, 193,
        128, 71,  130, 166, 130, 23,  75,  77,  201, 119, 168, 78,  178, 124, 31,  102, 79,  34,
        235, 100, 179, 171, 174, 67,  61,  4,   95,  180, 238, 163, 115, 11,  196, 239, 48,  208,
        251, 133, 152, 71,  29,  234, 44,  120, 246, 84,  235, 205, 237, 139, 116, 54,  21,  92,
        31,  3,   54,  46,  132, 9,   192, 99,  96,  34,  184, 17,  107, 206, 212, 196, 96,  153,
        197, 63,  164, 216, 216, 209, 244, 246, 190, 119, 117, 253, 68,  142, 168, 136, 68,  77,
        161, 2,   129, 129, 0,   241, 31,  184, 143, 133, 20,  32,  45,  78,  59,  19,  114, 112,
        243, 203, 152, 216, 225, 127, 201, 202, 247, 124, 118, 237, 169, 161, 188, 14,  44,  235,
        196, 195, 153, 122, 191, 150, 188, 219, 148, 91,  238, 222, 62,  1,   214, 70,  73,  19,
        244, 70,  213, 148, 33,  134, 119, 97,  158, 205, 181, 132, 182, 61,  202, 129, 202, 205,
        159, 169, 3,   10,  0,   213, 187, 20,  52,  131, 184, 170, 168, 106, 125, 134, 22,  173,
        193, 102, 69,  55,  108, 137, 4,   226, 89,  231, 132, 229, 252, 237, 55,  19,  94,  168,
        247, 118, 148, 12,  211, 55,  21,  80,  172, 220, 26,  242, 212, 9,   191, 193, 173, 114,
        83,  171, 21,  65,  84,  15,  221, 2,   129, 129, 0,   219, 54,  2,   81,  92,  22,  11,
        65,  128, 61,  115, 42,  251, 187, 143, 65,  31,  192, 36,  100, 137, 50,  228, 78,  125,
        216, 231, 40,  203, 254, 123, 197, 40,  42,  111, 87,  2,   121, 100, 200, 186, 34,  97,
        138, 131, 241, 22,  29,  24,  114, 81,  239, 213, 222, 59,  183, 200, 61,  80,  219, 98,
        149, 177, 57,  46,  158, 135, 194, 5,   118, 24,  88,  218, 237, 5,   115, 23,  216, 21,
        202, 254, 82,  37,  62,  175, 47,  114, 104, 151, 150, 94,  212, 111, 10,  33,  45,  131,
        85,  162, 210, 230, 72,  130, 233, 227, 33,  102, 204, 167, 228, 51,  108, 195, 178, 121,
        172, 224, 246, 122, 190, 225, 38,  227, 144, 135, 104, 46,  131, 2,   129, 129, 0,   236,
        9,   27,  72,  19,  3,   162, 131, 247, 34,  201, 100, 171, 193, 91,  186, 98,  4,   76,
        109, 163, 44,  37,  64,  222, 97,  193, 155, 47,  93,  53,  230, 197, 122, 198, 184, 41,
        188, 242, 78,  6,   184, 140, 1,   179, 22,  168, 114, 252, 255, 145, 31,  158, 4,   59,
        119, 61,  174, 144, 188, 114, 15,  91,  233, 146, 168, 142, 37,  14,  243, 148, 165, 64,
        148, 3,   177, 108, 136, 39,  54,  250, 23,  170, 93,  36,  246, 63,  64,  222, 130, 118,
        150, 187, 101, 58,  199, 211, 195, 134, 10,  246, 1,   33,  242, 44,  183, 188, 222, 61,
        251, 181, 159, 161, 79,  24,  10,  13,  9,   19,  116, 208, 135, 170, 224, 1,   181, 98,
        89,  2,   129, 129, 0,   17,  86,  25,  34,  212, 20,  142, 57,  84,  234, 7,   52,  172,
        9,   238, 79,  105, 50,  105, 238, 101, 135, 87,  212, 249, 80,  241, 31,  33,  218, 243,
        112, 233, 55,  73,  236, 232, 174, 47,  17,  76,  223, 49,  53,  162, 47,  171, 223, 11,
        50,  231, 85,  255, 100, 254, 246, 14,  233, 2,   127, 7,   49,  237, 125, 39,  57,  180,
        100, 220, 199, 181, 47,  57,  201, 42,  248, 42,  55,  149, 169, 163, 41,  93,  246, 178,
        38,  31,  119, 52,  29,  217, 76,  21,  168, 8,   109, 176, 8,   82,  195, 57,  33,  28,
        241, 96,  92,  32,  228, 40,  150, 252, 150, 42,  119, 239, 245, 131, 41,  27,  22,  3,
        122, 110, 222, 220, 70,  153, 255, 2,   129, 129, 0,   202, 220, 12,  189, 78,  79,  0,
        48,  30,  53,  148, 25,  5,   41,  200, 50,  76,  25,  237, 119, 19,  139, 117, 130, 40,
        138, 34,  159, 134, 198, 242, 97,  249, 91,  147, 212, 122, 49,  136, 86,  179, 88,  94,
        104, 177, 185, 11,  230, 200, 70,  122, 78,  143, 151, 246, 232, 32,  6,   79,  141, 39,
        147, 221, 249, 62,  28,  250, 17,  159, 31,  22,  109, 225, 93,  101, 136, 217, 232, 172,
        95,  253, 48,  201, 83,  55,  76,  34,  85,  125, 63,  128, 210, 73,  130, 66,  93,  254,
        0,   117, 76,  250, 184, 16,  200, 255, 18,  106, 223, 176, 153, 100, 211, 96,  209, 210,
        211, 55,  207, 48,  118, 197, 62,  77,  89,  249, 17,  254, 238};
    // Corresponding PKCS#1 public key (DER format)
    static const std::vector<uint8_t> public_key = {
        48,  130, 1,   34,  48,  13,  6,   9,   42,  134, 72,  134, 247, 13,  1,   1,   1,   5,
        0,   3,   130, 1,   15,  0,   48,  130, 1,   10,  2,   130, 1,   1,   0,   206, 121, 1,
        194, 150, 84,  247, 224, 78,  8,   2,   207, 100, 16,  201, 227, 84,  206, 11,  202, 175,
        166, 222, 37,  33,  228, 83,  240, 243, 248, 192, 118, 7,   56,  155, 188, 106, 171, 162,
        46,  65,  191, 245, 18,  68,  208, 167, 184, 125, 29,  39,  29,  39,  218, 152, 209, 107,
        50,  77,  10,  206, 128, 188, 156, 35,  108, 51,  194, 74,  150, 231, 0,   155, 78,  46,
        97,  141, 36,  73,  19,  4,   21,  228, 0,   28,  192, 142, 93,  172, 167, 181, 121, 78,
        214, 31,  238, 29,  181, 191, 135, 154, 41,  236, 224, 236, 42,  249, 39,  129, 158, 90,
        92,  55,  228, 92,  15,  195, 174, 19,  173, 243, 153, 40,  40,  228, 217, 125, 125, 123,
        91,  253, 122, 6,   49,  129, 47,  43,  173, 209, 186, 108, 111, 136, 207, 215, 103, 229,
        61,  100, 244, 122, 196, 246, 21,  37,  228, 53,  219, 98,  99,  86,  87,  15,  30,  2,
        255, 12,  228, 215, 187, 146, 189, 134, 94,  223, 208, 243, 151, 138, 124, 204, 5,   156,
        3,   74,  96,  101, 207, 145, 120, 33,  218, 224, 185, 167, 33,  223, 24,  139, 116, 65,
        81,  206, 140, 194, 137, 98,  91,  129, 134, 246, 138, 186, 82,  144, 184, 213, 104, 109,
        139, 127, 102, 35,  19,  40,  219, 154, 66,  213, 192, 60,  36,  104, 90,  9,   34,  170,
        158, 52,  217, 94,  100, 62,  17,  85,  85,  152, 214, 32,  204, 31,  113, 133, 165, 212,
        23,  2,   3,   1,   0,   1};
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
