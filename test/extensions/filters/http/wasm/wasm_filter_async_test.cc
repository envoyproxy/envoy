#include "test/extensions/filters/http/wasm/wasm_filter_test_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmHttpFilterTest,
                         Envoy::Extensions::Common::Wasm::host_test_runtime_and_language_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmHttpFilterTest, AsyncCall) {
  setupTest("async_call");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "11"}}),
                      message->headers());
            EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
            EXPECT_EQ(options.send_xff, false);
            callbacks = &cb;
            return &request;
          });

  EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response")));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(":status -> 200")));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        response_message->body().add("response");
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      });
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));

  EXPECT_NE(callbacks, nullptr);
}

TEST_P(WasmHttpFilterTest, StopAndResumeViaAsyncCall) {
  setupTest("resume_call");
  setupFilter();

  InSequence s;

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                  {":path", "/"},
                                                  {":authority", "foo"},
                                                  {"content-length", "6"}}),
                  message->headers());
        callbacks = &cb;
        return &request;
      });

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      });
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("continueRequest")));

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, continueDecoding()).WillOnce([&]() {
    // Verify that we're not resuming processing from within Wasm callback.
    EXPECT_EQ(proxy_wasm::current_context_, nullptr);
  });

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));

  EXPECT_NE(callbacks, nullptr);
}

TEST_P(WasmHttpFilterTest, AsyncCallBadCall) {
  setupTest("async_call");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/bad"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  // Just fail the send.
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        return nullptr;
      });

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("async_call rejected")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));
}

TEST_P(WasmHttpFilterTest, AsyncCallBadCluster) {
  setupTest("async_call");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/bad"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
        // Simulate code path for "no healthy host for HTTP connection pool" inline callback.
        cb.onSuccess(request, std::move(response));
        return nullptr;
      });

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("async_call rejected")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));
}

TEST_P(WasmHttpFilterTest, AsyncCallFailure) {
  setupTest("async_call");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                  {":path", "/"},
                                                  {":authority", "foo"},
                                                  {"content-length", "11"}}),
                  message->headers());
        EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
        callbacks = &cb;
        return &request;
      });

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        callbacks->onFailure(request, Http::AsyncClient::FailureReason::Reset);
        return proxy_wasm::WasmResult::Ok;
      });
  // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("async_call failed")));
  } else {
    EXPECT_CALL(rootContext(), log_(spdlog::level::info, Eq("async_call failed")));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));

  EXPECT_NE(callbacks, nullptr);
}

TEST_P(WasmHttpFilterTest, AsyncCallAfterDestroyed) {
  setupTest("async_call");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks = nullptr;
  cluster_manager_.initializeThreadLocalClusters({"cluster"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                    const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                  {":path", "/"},
                                                  {":authority", "foo"},
                                                  {"content-length", "11"}}),
                  message->headers());
        EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
        callbacks = &cb;
        return &request;
      });

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, false));

  EXPECT_CALL(request, cancel()).WillOnce([&]() { callbacks = nullptr; });

  // Destroy the Context, Plugin and VM.
  context_.reset();
  plugin_.reset();
  plugin_handle_.reset();
  wasm_.reset();

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response_message->body().add("response");

  // (Don't) Make the callback on the destroyed VM.
  EXPECT_EQ(callbacks, nullptr);
  if (callbacks) {
    callbacks->onSuccess(request, std::move(response_message));
  }
}

TEST_P(WasmHttpFilterTest, GrpcCall) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_call");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_call_proto");
  }
  for (const auto& id : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    NiceMock<Grpc::MockAsyncRequest> request;
    Grpc::RawAsyncRequestCallbacks* callbacks = nullptr;
    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    Tracing::Span* parent_span{};
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view service_full_name, absl::string_view method_name,
                      Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                      Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(service_full_name, "service");
          EXPECT_EQ(method_name, "method");
          Protobuf::Value value;
          EXPECT_TRUE(
              value.ParseFromArray(message->linearize(message->length()), message->length()));
          EXPECT_EQ(value.string_value(), "request");
          callbacks = &cb;
          parent_span = &span;
          EXPECT_EQ(options.timeout->count(), 1000);
          EXPECT_EQ(options.send_xff, false);
          return &request;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response")));
    }
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onSuccessRaw(std::move(response), span);
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallBadCall) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_call");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_call_proto");
  }
  for (const auto& id : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("expected failure occurred"))));
    }

    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                      Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                      const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
          return nullptr;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallFailure) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_call");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_call_proto");
  }
  for (const auto& id : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    NiceMock<Grpc::MockAsyncRequest> request;
    Grpc::RawAsyncRequestCallbacks* callbacks = nullptr;
    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    Tracing::Span* parent_span{};
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view service_full_name, absl::string_view method_name,
                      Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                      Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(service_full_name, "service");
          EXPECT_EQ(method_name, "method");
          Protobuf::Value value;
          EXPECT_TRUE(
              value.ParseFromArray(message->linearize(message->length()), message->length()));
          EXPECT_EQ(value.string_value(), "request");
          callbacks = &cb;
          parent_span = &span;
          EXPECT_EQ(options.timeout->count(), 1000);
          return &request;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("failure bad")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("failure bad")));
    }
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    // Test some additional error paths.
    // 0xFF00 (HTTP call).
    EXPECT_EQ(filter().grpcSend(0xFF00, "", false), proxy_wasm::WasmResult::BadArgument);
    EXPECT_EQ(filter().grpcCancel(0xFF00), proxy_wasm::WasmResult::BadArgument);
    EXPECT_EQ(filter().grpcClose(0xFF00), proxy_wasm::WasmResult::BadArgument);

    // 0xFF01 (gRPC call).
    EXPECT_EQ(filter().grpcSend(0xFF01, "", false), proxy_wasm::WasmResult::BadArgument);
    EXPECT_EQ(filter().grpcCancel(0xFF01), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcClose(0xFF01), proxy_wasm::WasmResult::NotFound);

    // 0xFF02 (gRPC stream).
    EXPECT_EQ(filter().grpcSend(0xFF02, "", false), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcCancel(0xFF02), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcClose(0xFF02), proxy_wasm::WasmResult::NotFound);

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onFailure(Grpc::Status::WellKnownGrpcStatus::Canceled, "bad", span);
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallCancel) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_call");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_call_proto");
  }
  for (const auto& id : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    NiceMock<Grpc::MockAsyncRequest> request;
    Grpc::RawAsyncRequestCallbacks* callbacks = nullptr;
    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    Tracing::Span* parent_span{};
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view service_full_name, absl::string_view method_name,
                      Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                      Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(service_full_name, "service");
          EXPECT_EQ(method_name, "method");
          Protobuf::Value value;
          EXPECT_TRUE(
              value.ParseFromArray(message->linearize(message->length()), message->length()));
          EXPECT_EQ(value.string_value(), "request");
          callbacks = &cb;
          parent_span = &span;
          EXPECT_EQ(options.timeout->count(), 1000);
          return &request;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    rootContext().onQueueReady(0);
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallClose) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_call");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_call_proto");
  }
  for (const auto& id : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    NiceMock<Grpc::MockAsyncRequest> request;
    Grpc::RawAsyncRequestCallbacks* callbacks = nullptr;
    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    Tracing::Span* parent_span{};
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view service_full_name, absl::string_view method_name,
                      Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                      Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(service_full_name, "service");
          EXPECT_EQ(method_name, "method");
          Protobuf::Value value;
          EXPECT_TRUE(
              value.ParseFromArray(message->linearize(message->length()), message->length()));
          EXPECT_EQ(value.string_value(), "request");
          callbacks = &cb;
          parent_span = &span;
          EXPECT_EQ(options.timeout->count(), 1000);
          return &request;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    rootContext().onQueueReady(1);
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallAfterDestroyed) {
  std::vector<std::pair<std::string, bool>> proto_or_cluster;
  proto_or_cluster.emplace_back("grpc_call", true);
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.emplace_back("grpc_call_proto", false);
  }
  for (const auto& [id, success] : proto_or_cluster) {
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("bogus grpc_service accepted error"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("bogus grpc_service rejected"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    Grpc::MockAsyncRequest request;
    Grpc::RawAsyncRequestCallbacks* callbacks = nullptr;
    Grpc::MockAsyncClientManager client_manager;
    auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    Tracing::Span* parent_span{};
    EXPECT_CALL(*async_client, sendRaw(_, _, _, _, _, _))
        .WillOnce([&](absl::string_view service_full_name, absl::string_view method_name,
                      Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                      Tracing::Span& span,
                      const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
          EXPECT_EQ(service_full_name, "service");
          EXPECT_EQ(method_name, "method");
          Protobuf::Value value;
          EXPECT_TRUE(
              value.ParseFromArray(message->linearize(message->length()), message->length()));
          EXPECT_EQ(value.string_value(), "request");
          callbacks = &cb;
          parent_span = &span;
          EXPECT_EQ(options.timeout->count(), 1000);
          return &request;
        });
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce([&]() -> Grpc::AsyncClientManager& { return client_manager; });
    EXPECT_CALL(client_manager, getOrCreateRawAsyncClient(_, _, _))
        .WillOnce([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
          return std::move(async_client);
        });
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(request, cancel()).WillOnce([&]() { callbacks = nullptr; });

      // Destroy the Context, Plugin and VM.
      context_.reset();
      plugin_.reset();
      plugin_handle_.reset();
      wasm_.reset();
    } else {
      rootContext().onQueueReady(2);

      // Start shutdown sequence.
      wasm_->wasm()->startShutdown();
      plugin_.reset();
      plugin_handle_.reset();
      wasm_.reset();
    }

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_EQ(callbacks, nullptr);
    } else {
      EXPECT_NE(callbacks, nullptr);
    }
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      if (success) {
        callbacks->onSuccessRaw(std::move(response), span);
      } else {
        callbacks->onFailure(Grpc::Status::WellKnownGrpcStatus::Canceled, "bad", span);
      }
    }
  }
}

void WasmHttpFilterTest::setupGrpcStreamTest(Grpc::RawAsyncStreamCallbacks*& callbacks,
                                             std::string id) {
  setupTest(id);
  setupFilter();

  EXPECT_CALL(async_client_manager_, getOrCreateRawAsyncClient(_, _, _))
      .WillRepeatedly(
          [&](const GrpcService&, Stats::Scope&, bool) -> Grpc::RawAsyncClientSharedPtr {
            auto async_client = std::make_unique<Grpc::MockAsyncClient>();
            EXPECT_CALL(*async_client, startRaw(_, _, _, _))
                .WillRepeatedly(
                    [&](absl::string_view service_full_name, absl::string_view method_name,
                        Grpc::RawAsyncStreamCallbacks& cb,
                        const Http::AsyncClient::StreamOptions& options) -> Grpc::RawAsyncStream* {
                      EXPECT_EQ(service_full_name, "service");
                      EXPECT_EQ(options.send_xff, false);
                      if (method_name != "method") {
                        return nullptr;
                      }
                      callbacks = &cb;
                      return &async_stream_;
                    });
            return async_client;
          });
  EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
      .WillRepeatedly([&]() -> Grpc::AsyncClientManager& { return async_client_manager_; });
}

TEST_P(WasmHttpFilterTest, GrpcStream) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_stream");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_stream_proto");
  }
  for (const auto& id : proto_or_cluster) {
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response response")));
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("close done")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close done")));
    }

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onReceiveTrailingMetadata(std::make_unique<Http::TestResponseTrailerMapImpl>());
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "done");
    }
  }
}

// Local close followed by remote close.

TEST_P(WasmHttpFilterTest, GrpcStreamCloseLocal) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_stream");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_stream_proto");
  }
  for (const auto& id : proto_or_cluster) {
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response close")));
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("close ok")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response close")));
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close ok")));
    }

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("close");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "ok");
    }
  }
}

// Remote close followed by local close.

TEST_P(WasmHttpFilterTest, GrpcStreamCloseRemote) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_stream");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_stream_proto");
  }
  for (const auto& id : proto_or_cluster) {
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response response")));
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("close close")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close close")));
    }

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "close");
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcStreamCancel) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_stream");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_stream_proto");
  }
  for (const auto& id : proto_or_cluster) {
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl{{"test", "reset"}}));
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcStreamOpenAtShutdown) {
  std::vector<std::string> proto_or_cluster;
  proto_or_cluster.push_back("grpc_stream");
  if (std::get<1>(GetParam()) == "cpp") {
    // cluster definition passed as a protobuf is only supported in C++ SDK.
    proto_or_cluster.push_back("grpc_stream_proto");
  }
  for (const auto& id : proto_or_cluster) {
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    // TODO(PiotrSikora): Switching back to the original context is inconsistent between SDKs.
    if (std::get<1>(GetParam()) == "rust") {
      EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response response")));
    } else {
      EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
    }

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    Protobuf::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      const auto source = request_headers.get(Http::LowerCaseString{"source"});
      EXPECT_EQ(source.size(), 1);
      EXPECT_EQ(source[0]->value().getStringView(), id);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onReceiveTrailingMetadata(std::make_unique<Http::TestResponseTrailerMapImpl>());
    }

    // Destroy the Context, Plugin and VM.
    context_.reset();
    plugin_.reset();
    plugin_handle_.reset();
    wasm_.reset();
  }
}

// Test metadata access including CEL expressions.

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
