#include "source/common/common/cleanup.h"

#include "test/extensions/filters/http/wasm/wasm_filter_test_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmHttpFilterTest,
                         Envoy::Extensions::Common::Wasm::host_test_runtime_and_language_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmHttpFilterTest, BadCode) {
  setupBase(std::get<0>(GetParam()), "bad code", createContextFn());
  EXPECT_EQ(wasm_, nullptr);
}

// Script touching headers only, request that is headers only.

TEST_P(WasmHttpFilterTest, HeadersOnlyRequestHeadersOnlyWithEnvVars) {
  envoy::extensions::wasm::v3::EnvironmentVariables envs;
  const std::string host_env_key = "ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV";
  if (std::get<0>(GetParam()) != "null") {
    // Setup env vars.
    const std::string host_env_value = "foo";
    const std::string env_key = "ENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV";
    const std::string env_value = "bar";
    TestEnvironment::setEnvVar(host_env_key, host_env_value, 0);
    envs.mutable_host_env_keys()->Add(host_env_key.c_str());
    (*envs.mutable_key_values())[env_key] = env_value;
  }
  Cleanup unset_env([&host_env_key]() { TestEnvironment::unsetEnvVar(host_env_key); });
  setupTest("", "headers", envs);
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  if (std::get<0>(GetParam()) != "null") {
    EXPECT_CALL(filter(),
                log_(spdlog::level::trace, Eq("ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV: foo\n"
                                              "ENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV: bar")));
  }
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  // Verify that route cache is cleared when modifying HTTP request headers.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks.downstream_callbacks_, clearRouteCache()).Times(2);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm"));
  // Test some errors.
  EXPECT_EQ(filter().continueStream(static_cast<proxy_wasm::WasmStreamType>(9999)),
            proxy_wasm::WasmResult::BadArgument);
  EXPECT_EQ(filter().closeStream(static_cast<proxy_wasm::WasmStreamType>(9999)),
            proxy_wasm::WasmResult::BadArgument);
  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(filter().encode1xxHeaders(response_headers), Http::Filter1xxHeadersStatus::Continue);
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AllHeadersAndTrailers) {
  setupTest("", "headers");
  setupFilter();

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  // Verify that route cache is cleared when modifying HTTP request headers.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks.downstream_callbacks_, clearRouteCache()).Times(2);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm"));
  Http::TestRequestTrailerMapImpl request_trailers{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter().decodeTrailers(request_trailers));
  Http::MetadataMap request_metadata{};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter().decodeMetadata(request_metadata));

  // Verify that route cache is NOT cleared when modifying HTTP response headers.
  EXPECT_CALL(decoder_callbacks.downstream_callbacks_, clearRouteCache()).Times(0);

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));
  EXPECT_THAT(response_headers.get_("test-status"), Eq("OK"));
  Http::TestResponseTrailerMapImpl response_trailers{};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter().encodeTrailers(response_trailers));
  Http::MetadataMap response_metadata{};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter().encodeMetadata(response_metadata));
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AddTrailers) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody data")))).Times(2);
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm"));

  Buffer::OwnedImpl data("data");
  Http::TestRequestTrailerMapImpl request_trailers{};
  EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).Times(0);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, false));
  EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).WillOnce(ReturnRef(request_trailers));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  EXPECT_THAT(request_trailers.get_("newtrailer"), Eq("request"));

  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));
  EXPECT_THAT(response_headers.get_("test-status"), Eq("OK"));

  Http::TestResponseTrailerMapImpl response_trailers{};
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).Times(0);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, false));
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(response_trailers));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  EXPECT_THAT(response_trailers.get_("newtrailer"), Eq("response"));

  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AllHeadersAndTrailersNotStarted) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  Http::TestRequestTrailerMapImpl request_trailers{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter().decodeTrailers(request_trailers));
  Http::MetadataMap request_metadata{};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter().decodeMetadata(request_metadata));
  Http::TestResponseHeaderMapImpl response_headers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));
  Http::TestResponseTrailerMapImpl response_trailers{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter().encodeTrailers(response_trailers));
  Http::MetadataMap response_metadata{};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter().encodeMetadata(response_metadata));
  Buffer::OwnedImpl data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, false));
  filter().onDestroy();
}

// Script touching headers only, request that is headers only.

TEST_P(WasmHttpFilterTest, HeadersOnlyRequestHeadersAndBody) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  EXPECT_FALSE(filter().endOfStream(proxy_wasm::WasmStreamType::Request));
  Buffer::OwnedImpl data("hello");
  Http::TestRequestTrailerMapImpl request_trailers{};
  EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).WillOnce(ReturnRef(request_trailers));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  EXPECT_THAT(request_trailers.get_("newtrailer"), Eq("request"));
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, HeadersStopAndContinue) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): This hand off is not currently possible in the Rust SDK.
    return;
  }
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy-wasm-pause"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, true));
  root_context_->onTick(0);
  filter().clearRouteCache();
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm-continue"));
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, HeadersStopAndContinueAllowStopIteration) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): This hand off is not currently possible in the Rust SDK.
    return;
  }
  setAllowOnHeadersStopIteration(true);
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy-wasm-pause"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter().decodeHeaders(request_headers, true));
  root_context_->onTick(0);
  filter().clearRouteCache();
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm-continue"));
  filter().onDestroy();
}

#if 0

TEST_P(WasmHttpFilterTest, HeadersStopAndEndStream) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): This hand off is not currently possible in the Rust SDK.
    return;
  }
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"server", "envoy-wasm-end-stream"}};
  EXPECT_EQ(Http::FilterHeadersStatus::ContinueAndEndStream,
            filter().decodeHeaders(request_headers, true));
  root_context_->onTick(0);
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm-continue"));
  filter().onDestroy();
}
#endif

TEST_P(WasmHttpFilterTest, HeadersStopAndBuffer) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): This hand off is not currently possible in the Rust SDK.
    return;
  }
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"server", "envoy-wasm-stop-buffer"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter().decodeHeaders(request_headers, true));
  root_context_->onTick(0);
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm-continue"));
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, HeadersStopAndWatermark) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): This hand off is not currently possible in the Rust SDK.
    return;
  }
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"server", "envoy-wasm-stop-watermark"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter().decodeHeaders(request_headers, true));
  root_context_->onTick(0);
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm-continue"));
  filter().onDestroy();
}

// Script that reads the body.

TEST_P(WasmHttpFilterTest, AccessLog) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onLog 2 / 200"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  filter().continueStream(proxy_wasm::WasmStreamType::Response);
  filter().closeStream(proxy_wasm::WasmStreamType::Response);
  StreamInfo::MockStreamInfo log_stream_info;
  EXPECT_CALL(log_stream_info, requestComplete())
      .WillRepeatedly(testing::Return(std::chrono::milliseconds(30)));
  filter().log({&request_headers, &response_headers, &response_trailers}, log_stream_info);
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AccessLogClientDisconnected) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onLog 2 / "))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  StreamInfo::MockStreamInfo log_stream_info;
  EXPECT_CALL(log_stream_info, requestComplete())
      .WillRepeatedly(testing::Return(std::chrono::milliseconds(30)));
  filter().log({&request_headers}, log_stream_info);
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AccessLogDisabledForIncompleteStream) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::debug, Eq(absl::string_view("onRequestHeaders 2 headers"))));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(absl::string_view("header path /"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onLog 2 / ")))).Times(0);
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  StreamInfo::MockStreamInfo log_stream_info;
  EXPECT_CALL(log_stream_info, requestComplete()).WillRepeatedly(testing::Return(std::nullopt));
  filter().log({&request_headers}, log_stream_info);
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AccessLogCreate) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onLog 2 / 200"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  StreamInfo::MockStreamInfo log_stream_info;
  EXPECT_CALL(log_stream_info, requestComplete())
      .WillRepeatedly(testing::Return(std::chrono::milliseconds(30)));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  filter().log({&request_headers, &response_headers, &response_trailers}, log_stream_info);
  filter().onDestroy();
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
