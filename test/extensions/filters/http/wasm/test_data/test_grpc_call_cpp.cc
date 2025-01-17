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

class MyGrpcCallHandler : public GrpcCallHandler<google::protobuf::Value> {
public:
  MyGrpcCallHandler() = default;
  void onSuccess(size_t body_size) override {
    if (call_done_) {
      proxy_done();
      return;
    }
    auto response = getBufferBytes(WasmBufferType::GrpcReceiveBuffer, 0, body_size);
    logDebug(response->proto<google::protobuf::Value>().string_value());
    cancel();
  }
  void onFailure(GrpcStatus) override {
    if (call_done_) {
      proxy_done();
      return;
    }
    auto p = getStatus();
    logDebug(std::string("failure ") + std::string(p.second->view()));
  }
  bool call_done_{false};
};

class GrpcCallRootContext : public RootContext {
public:
  explicit GrpcCallRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}

  void onQueueReady(uint32_t op) override {
    if (op == 0) {
      handler_->cancel();
    } else if (op == 1) {
      grpcClose(handler_->token());
    } else if (op == 2) {
      on_done_ = false;
      handler_->call_done_ = true;
    }
  }

  bool onDone() override {
    return on_done_;
  }

  MyGrpcCallHandler* handler_ = nullptr;
  bool on_done_{true};
};

class GrpcCallContextProto : public Context {
public:
  explicit GrpcCallContextProto(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;

  GrpcCallRootContext* root() { return static_cast<GrpcCallRootContext*>(Context::root()); }
};

static RegisterContextFactory register_GrpcCallContextProto(CONTEXT_FACTORY(GrpcCallContextProto),
                                                       ROOT_FACTORY(GrpcCallRootContext),
                                                       "grpc_call_proto");

FilterHeadersStatus GrpcCallContextProto::onRequestHeaders(uint32_t, bool end_of_stream) {
  GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("cluster");
  std::string grpc_service_string;
  grpc_service.SerializeToString(&grpc_service_string);
  google::protobuf::Value value;
  value.set_string_value("request");
  HeaderStringPairs initial_metadata;
  initial_metadata.push_back(std::make_pair<std::string, std::string>("source", "grpc_call_proto"));
  root()->handler_ = new MyGrpcCallHandler();
  if (root()->grpcCallHandler(
          "bogus grpc_service", "service", "method", initial_metadata, value, 1000,
          std::unique_ptr<GrpcCallHandlerBase>(new MyGrpcCallHandler())) == WasmResult::ParseFailure) {
    logError("bogus grpc_service accepted error");
  }
  if (end_of_stream) {
    if (root()->grpcCallHandler(grpc_service_string, "service", "method", initial_metadata, value,
                                1000, std::unique_ptr<GrpcCallHandlerBase>(root()->handler_)) ==
        WasmResult::Ok) {
      logError("expected failure did not occur");
    }
    return FilterHeadersStatus::Continue;
  }
  root()->grpcCallHandler(grpc_service_string, "service", "method", initial_metadata, value, 1000,
                          std::unique_ptr<GrpcCallHandlerBase>(root()->handler_));
  return FilterHeadersStatus::StopIteration;
}

class GrpcCallContext : public Context {
public:
  explicit GrpcCallContext(uint32_t id, RootContext* root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t, bool) override;

  GrpcCallRootContext* root() { return static_cast<GrpcCallRootContext*>(Context::root()); }
};

static RegisterContextFactory register_GrpcCallContext(CONTEXT_FACTORY(GrpcCallContext),
                                                       ROOT_FACTORY(GrpcCallRootContext),
                                                       "grpc_call");

FilterHeadersStatus GrpcCallContext::onRequestHeaders(uint32_t, bool end_of_stream) {
  google::protobuf::Value value;
  value.set_string_value("request");
  HeaderStringPairs initial_metadata;
  initial_metadata.push_back(std::make_pair<std::string, std::string>("source", "grpc_call"));
  root()->handler_ = new MyGrpcCallHandler();
  if (root()->grpcCallHandler(
          "bogus grpc_service", "service", "method", initial_metadata, value, 1000,
          std::unique_ptr<GrpcCallHandlerBase>(new MyGrpcCallHandler())) == WasmResult::ParseFailure) {
    logError("bogus grpc_service rejected");
  }
  if (end_of_stream) {
    if (root()->grpcCallHandler("cluster", "service", "method", initial_metadata, value,
                                1000, std::unique_ptr<GrpcCallHandlerBase>(root()->handler_)) ==
        WasmResult::InternalFailure) {
      logError("expected failure occurred");
    }
    return FilterHeadersStatus::Continue;
  }
  if (root()->grpcCallHandler("cluster", "service", "method", initial_metadata, value, 1000,
                          std::unique_ptr<GrpcCallHandlerBase>(root()->handler_)) == WasmResult::Ok) {
    logError("cluster call succeeded");
  }
  return FilterHeadersStatus::StopIteration;
}

END_WASM_PLUGIN
