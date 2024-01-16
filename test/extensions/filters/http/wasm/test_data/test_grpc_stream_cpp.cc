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

class GrpcStreamContextProto : public Context {
public:
  explicit GrpcStreamContextProto(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

class GrpcStreamRootContext : public RootContext {
public:
  explicit GrpcStreamRootContext(uint32_t id, std::string_view root_id)
      : RootContext(id, root_id) {}
};

static RegisterContextFactory register_GrpcStreamContextProto(CONTEXT_FACTORY(GrpcStreamContextProto),
                                                         ROOT_FACTORY(GrpcStreamRootContext),
                                                         "grpc_stream_proto");

class MyGrpcStreamHandler
    : public GrpcStreamHandler<google::protobuf::Value, google::protobuf::Value> {
public:
  MyGrpcStreamHandler() = default;
  void onReceiveInitialMetadata(uint32_t) override {
    auto h = getHeaderMapValue(WasmHeaderMapType::GrpcReceiveInitialMetadata, "test");
    if (h->view() == "reset") {
      reset();
      return;
    }
    // Not Found.
    h = getHeaderMapValue(WasmHeaderMapType::HttpCallResponseHeaders, "foo");
    h = getHeaderMapValue(WasmHeaderMapType::HttpCallResponseTrailers, "foo");
    addHeaderMapValue(WasmHeaderMapType::GrpcReceiveInitialMetadata, "foo", "bar");
  }
  void onReceive(size_t body_size) override {
    auto response = getBufferBytes(WasmBufferType::GrpcReceiveBuffer, 0, body_size);
    auto response_string = response->proto<google::protobuf::Value>().string_value();
    google::protobuf::Value message;
    if (response_string == "close") {
      close();
    } else {
      send(message, false);
    }
    logDebug(std::string("response ") + response_string);
  }
  void onReceiveTrailingMetadata(uint32_t) override {
    auto h = getHeaderMapValue(WasmHeaderMapType::GrpcReceiveTrailingMetadata, "foo");
    addHeaderMapValue(WasmHeaderMapType::GrpcReceiveTrailingMetadata, "foo", "bar");
  }
  void onRemoteClose(GrpcStatus) override {
    auto p = getStatus();
    logDebug(std::string("close ") + std::string(p.second->view()));
    if (p.second->view() == "close") {
      close();
    } else if (p.second->view() == "ok") {
      return;
    } else {
      reset();
    }
  }
};

FilterHeadersStatus GrpcStreamContextProto::onRequestHeaders(uint32_t, bool) {
  GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("cluster");
  std::string grpc_service_string;
  grpc_service.SerializeToString(&grpc_service_string);
  HeaderStringPairs initial_metadata;
  initial_metadata.push_back(
      std::make_pair<std::string, std::string>("source", "grpc_stream_proto"));
  if (root()->grpcStreamHandler("bogus service string", "service", "method", initial_metadata,
                                std::unique_ptr<GrpcStreamHandlerBase>(
                                    new MyGrpcStreamHandler())) == WasmResult::ParseFailure) {
    logError("expected bogus service parse failure");
  }
  if (root()->grpcStreamHandler(grpc_service_string, "service", "bad method", initial_metadata,
                                std::unique_ptr<GrpcStreamHandlerBase>(
                                    new MyGrpcStreamHandler())) == WasmResult::InternalFailure) {
    logError("expected bogus method call failure");
  }
  if (root()->grpcStreamHandler(grpc_service_string, "service", "method", initial_metadata,
                            std::unique_ptr<GrpcStreamHandlerBase>(new MyGrpcStreamHandler())) == WasmResult::Ok) {
    logError("cluster call succeeded");
  }
  return FilterHeadersStatus::StopIteration;
}

class GrpcStreamContext : public Context {
public:
  explicit GrpcStreamContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;
};

static RegisterContextFactory register_GrpcStreamContext(CONTEXT_FACTORY(GrpcStreamContext),
                                                         ROOT_FACTORY(GrpcStreamRootContext),
                                                         "grpc_stream");

FilterHeadersStatus GrpcStreamContext::onRequestHeaders(uint32_t, bool) {
  HeaderStringPairs initial_metadata;
  initial_metadata.push_back(std::make_pair<std::string, std::string>("source", "grpc_stream"));
  if (root()->grpcStreamHandler("bogus service string", "service", "method", initial_metadata,
                                std::unique_ptr<GrpcStreamHandlerBase>(
                                    new MyGrpcStreamHandler())) == WasmResult::ParseFailure) {
    logError("expected bogus service parse failure");
  }
  if (root()->grpcStreamHandler("cluster", "service", "bad method", initial_metadata,
                                std::unique_ptr<GrpcStreamHandlerBase>(
                                    new MyGrpcStreamHandler())) == WasmResult::InternalFailure) {
    logError("expected bogus method call failure");
  }
  if (root()->grpcStreamHandler("cluster", "service", "method", initial_metadata,
                            std::unique_ptr<GrpcStreamHandlerBase>(new MyGrpcStreamHandler())) == WasmResult::Ok) {
    logError("cluster call succeeded");
  }
  return FilterHeadersStatus::StopIteration;
}

END_WASM_PLUGIN
