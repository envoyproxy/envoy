#include "test/extensions/filters/http/wasm/wasm_filter_test_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmHttpFilterTest,
                         Envoy::Extensions::Common::Wasm::host_test_runtime_and_language_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmHttpFilterTest, Metadata) {
#ifdef WIN32
  // TODO: re-enable this on Windows if and when the CEL `Antlr` parser compiles on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  setupTest("", "metadata");
  setupFilter();
  envoy::config::core::v3::Node node_data;
  Protobuf::Value node_val;
  node_val.set_string_value("wasm_node_get_value");
  (*node_data.mutable_metadata()->mutable_fields())["wasm_node_get_key"] = node_val;
  (*node_data.mutable_metadata()->mutable_fields())["wasm_node_list_key"] =
      ValueUtil::listValue({node_val});
  EXPECT_CALL(local_info_, node()).WillRepeatedly(ReturnRef(node_data));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::debug, Eq(absl::string_view("onTick wasm_node_get_value"))));

  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody wasm_node_get_value"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::trace,
                   Eq(absl::string_view("Struct wasm_request_get_value wasm_request_get_value"))));
  if (std::get<1>(GetParam()) != "rust") {
    // TODO(PiotrSikora): not yet supported in the Rust SDK.
    EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("server is envoy-wasm"))));
  }

  request_stream_info_.metadata_.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, Protobuf::Struct>(
          "envoy.filters.http.wasm",
          MessageUtil::keyValueStruct("wasm_request_get_key", "wasm_request_get_value")));

  rootContext().onTick(0);

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  std::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
  EXPECT_CALL(request_stream_info_, requestComplete()).WillRepeatedly(Return(dur));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("duration is 15000000"))));
  if (std::get<1>(GetParam()) != "rust") {
    // TODO(PiotrSikora): not yet supported in the Rust SDK.
    EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("grpc service: test"))));
  }
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"biz", "baz"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));

  filter().log({&request_headers}, request_stream_info_);

  const auto* result =
      request_stream_info_.filterState()->getDataReadOnly<Filters::Common::Expr::CelState>(
          "wasm.wasm_request_set_key");
  EXPECT_EQ("wasm_request_set_value", result->value());

  filter().onDestroy();
  filter().onDestroy(); // Does nothing.
}

TEST_P(WasmHttpFilterTest, Property) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): test not yet implemented using Rust SDK.
    return;
  }
  envoy::config::core::v3::Node node_data;
  Protobuf::Value node_val;
  node_val.set_string_value("sample_data");
  (*node_data.mutable_metadata()->mutable_fields())["istio.io/metadata"] = node_val;
  EXPECT_CALL(local_info_, node()).WillRepeatedly(ReturnRef(node_data));
  setupTest("", "property");
  setupFilter();
  request_stream_info_.metadata_.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, Protobuf::Struct>(
          "envoy.filters.http.wasm",
          MessageUtil::keyValueStruct("wasm_request_get_key", "wasm_request_get_value")));
  EXPECT_CALL(request_stream_info_, responseCode()).WillRepeatedly(Return(403));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));

  // test outputs should match inputs
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("request.path: /test_context"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("node.metadata: sample_data"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("metadata: wasm_request_get_value"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("response.code: 403"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("state: wasm_value"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("upstream host metadata: endpoint"))));

  root_context_->onTick(0);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/test_context"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
  request_stream_info_.route_name_ = "route12";
  request_stream_info_.downstream_connection_info_provider_->setRequestedServerName("w3.org");
  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(connection, id()).WillRepeatedly(Return(4));
  EXPECT_CALL(encoder_callbacks_, connection())
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection}));
  std::shared_ptr<Router::MockRoute> route{new NiceMock<Router::MockRoute>()};
  request_stream_info_.route_ = route;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host_description(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            key: endpoint
      )EOF"));
  EXPECT_CALL(*host_description, metadata()).WillRepeatedly(Return(metadata));
  request_stream_info_.upstreamInfo()->setUpstreamHost(host_description);
  EXPECT_CALL(request_stream_info_, requestComplete())
      .WillRepeatedly(Return(std::chrono::milliseconds(30)));
  filter().log({&request_headers}, request_stream_info_);
}

TEST_P(WasmHttpFilterTest, ClusterMetadata) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "cluster_metadata");
  setupFilter();
  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  auto cluster_metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
      filter_metadata:
        namespace:
          key: cluster
    )EOF"));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host_description(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  Http::TestRequestHeaderMapImpl request_headers{{}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(*cluster, metadata()).WillRepeatedly(ReturnRef(*cluster_metadata));
  EXPECT_CALL(request_stream_info_, requestComplete)
      .WillRepeatedly(Return(std::chrono::milliseconds(30)));
  request_stream_info_.upstream_cluster_info_ = cluster;
  EXPECT_CALL(request_stream_info_, upstreamClusterInfo()).Times(testing::AnyNumber());
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("cluster metadata: cluster"))));
  filter().log({&request_headers}, request_stream_info_);
}

TEST_P(WasmHttpFilterTest, SharedData) {
  setupTest("shared_data");
  EXPECT_CALL(rootContext(), log_(spdlog::level::info, Eq(absl::string_view("set CasMismatch"))));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::debug, Eq(absl::string_view("get 1 shared_data_value1"))));
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_CALL(rootContext(),
                log_(spdlog::level::warn, Eq(absl::string_view("get 2 shared_data_value2"))));
  } else {
    EXPECT_CALL(rootContext(),
                log_(spdlog::level::critical, Eq(absl::string_view("get 2 shared_data_value2"))));
  }
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::debug, Eq(absl::string_view("get of bad key not found"))));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::debug, Eq(absl::string_view("second get of bad key not found"))));
  rootContext().onTick(0);
  rootContext().onQueueReady(0);
}

TEST_P(WasmHttpFilterTest, SharedQueue) {
  setupTest("shared_queue");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn,
                   Eq(absl::string_view("onRequestHeaders not found self/bad_shared_queue"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn,
                   Eq(absl::string_view("onRequestHeaders not found vm_id/bad_shared_queue"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn,
                   Eq(absl::string_view("onRequestHeaders not found bad_vm_id/bad_shared_queue"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn,
                             Eq(absl::string_view("onRequestHeaders found self/my_shared_queue"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn,
                   Eq(absl::string_view("onRequestHeaders found vm_id/my_shared_queue"))));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("onRequestHeaders enqueue Ok"))));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::warn, Eq(absl::string_view("onQueueReady bad token not found"))))
      .Times(2);
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::warn, Eq(absl::string_view("onQueueReady extra data not found"))))
      .Times(2);
  EXPECT_CALL(rootContext(), log_(spdlog::level::info, Eq(absl::string_view("onQueueReady"))))
      .Times(2);
  EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq(absl::string_view("data data1 Ok"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
  auto token = proxy_wasm::resolveQueueForTest("vm_id", "my_shared_queue");
  root_context_->onQueueReady(token);
}

// Script using a root_id which is not registered.

TEST_P(WasmHttpFilterTest, RootIdNotRegistered) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): proxy_get_property("root_id") is not yet supported in the Rust SDK.
    return;
  }
  setupTest();
  setupFilter();
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
}

// Script using an explicit root_id which is registered.

TEST_P(WasmHttpFilterTest, RootId1) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): proxy_get_property("root_id") is not yet supported in the Rust SDK.
    return;
  }
  setupTest("context1");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders1 2"))));
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
}

// Script using an explicit root_id which is registered.

TEST_P(WasmHttpFilterTest, RootId2) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): proxy_get_property("root_id") is not yet supported in the Rust SDK.
    return;
  }
  setupTest("context2");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders2 2"))));
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, true));
}

TEST_P(WasmHttpFilterTest, PanicOnRequestHeaders) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();
  auto headers = Http::TestResponseHeaderMapImpl{{":status", "503"}};
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&headers), true));

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().onRequestHeaders(0, false));
}

TEST_P(WasmHttpFilterTest, PanicOnRequestBody) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();
  auto headers = Http::TestResponseHeaderMapImpl{{":status", "503"}};

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&headers), true));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterDataStatus::StopIterationNoBuffer, filter().onRequestBody(0, false));
}

TEST_P(WasmHttpFilterTest, PanicOnRequestTrailers) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();
  auto headers = Http::TestResponseHeaderMapImpl{{":status", "503"}};
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&headers), true));

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterTrailersStatus::StopIteration, filter().onRequestTrailers(0));
}

TEST_P(WasmHttpFilterTest, PanicOnResponseHeaders) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().onResponseHeaders(0, false));
}

TEST_P(WasmHttpFilterTest, PanicOnResponseBody) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterDataStatus::StopIterationNoBuffer, filter().onResponseBody(0, false));
}

TEST_P(WasmHttpFilterTest, PanicOnResponseTrailers) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  setupTest("panic");
  setupFilter();

  // In the case of VM failure, failStream is called for both request and response stream types,
  // so we need to make sure that we don't send the local reply twice.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Envoy::Http::Code::ServiceUnavailable, Eq(""), _,
                                                 Eq(Grpc::Status::WellKnownGrpcStatus::Unavailable),
                                                 Eq("wasm_fail_stream")));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterTrailersStatus::StopIteration, filter().onResponseTrailers(0));
}

TEST_P(WasmHttpFilterTest, CloseRequest) {
  setupTest("close_stream");
  setupFilter();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
  EXPECT_CALL(stream_info, setResponseCodeDetails("wasm_close_stream"));
  EXPECT_CALL(decoder_callbacks, resetStream(_, _));

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterHeadersStatus::Continue, filter().onRequestHeaders(0, false));
}

TEST_P(WasmHttpFilterTest, CloseResponse) {
  setupTest("close_stream");
  setupFilter();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  filter().setEncoderFilterCallbacks(encoder_callbacks);
  EXPECT_CALL(encoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
  EXPECT_CALL(stream_info, setResponseCodeDetails("wasm_close_stream"));
  EXPECT_CALL(encoder_callbacks, resetStream(_, _));

  // Create in-VM context.
  filter().onCreate();
  EXPECT_EQ(proxy_wasm::FilterHeadersStatus::Continue, filter().onResponseHeaders(0, false));
}

TEST_P(WasmHttpFilterTest, VerifySignature) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(patricio78): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "verify_signature");
  setupFilter();
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq(absl::string_view("signature is valid"))));

  EXPECT_CALL(rootContext(),
              log_(spdlog::level::err, Eq(absl::string_view("unknown is not supported."))));
  EXPECT_CALL(rootContext(), log_(spdlog::level::err,
                                  Eq(absl::string_view("Failed to initialize digest verify."))))
      .Times(2);
  rootContext().onTick(0);
}

TEST_P(WasmHttpFilterTest, Sign) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(patricio78): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "sign");
  setupFilter();
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info, Eq(absl::string_view("signature created successfully"))));

  EXPECT_CALL(rootContext(),
              log_(spdlog::level::err, Eq(absl::string_view("unknown is not supported."))));
  EXPECT_CALL(
      rootContext(),
      log_(spdlog::level::err,
           Eq(absl::string_view("Invalid key type: private key required for signing operation."))));
  rootContext().onTick(0);
}

TEST_P(WasmHttpFilterTest, SignAndVerifySignature) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(patricio78): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "sign_and_verify_signature");
  setupFilter();
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info,
                   Eq(absl::string_view("signature created successfully, length: 256"))));
  EXPECT_CALL(rootContext(),
              log_(spdlog::level::info,
                   Eq(absl::string_view(
                       "end-to-end test passed: signature created and verified successfully"))));
  EXPECT_CALL(
      rootContext(),
      log_(spdlog::level::info,
           Eq(absl::string_view("mutual exclusion test passed: both PEM and DER keys rejected"))));
  rootContext().onTick(0);
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
