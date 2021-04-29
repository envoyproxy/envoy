#include "common/http/message_impl.h"

#include "extensions/filters/http/wasm/wasm_filter.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/wasm_base.h"

using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

MATCHER_P(MapEq, rhs, "") {
  const Envoy::ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(rhs.size() > 0);
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

using BufferFunction = std::function<void(::Envoy::Buffer::Instance&)>;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::CreateContextFn;
using Envoy::Extensions::Common::Wasm::Plugin;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;
using Envoy::Extensions::Common::Wasm::WasmHandleSharedPtr;
using proxy_wasm::ContextBase;
using GrpcService = envoy::config::core::v3::GrpcService;
using WasmFilterConfig = envoy::extensions::filters::http::wasm::v3::Wasm;

class TestFilter : public Envoy::Extensions::Common::Wasm::Context {
public:
  TestFilter(Wasm* wasm, uint32_t root_context_id,
             Envoy::Extensions::Common::Wasm::PluginSharedPtr plugin)
      : Envoy::Extensions::Common::Wasm::Context(wasm, root_context_id, plugin) {}
  MOCK_CONTEXT_LOG_;
};

class TestRoot : public Envoy::Extensions::Common::Wasm::Context {
public:
  TestRoot(Wasm* wasm, const std::shared_ptr<Plugin>& plugin) : Context(wasm, plugin) {}
  MOCK_CONTEXT_LOG_;
};

class WasmHttpFilterTest : public Common::Wasm::WasmHttpFilterTestBase<
                               testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  WasmHttpFilterTest() = default;
  ~WasmHttpFilterTest() override = default;

  CreateContextFn createContextFn() {
    return [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
      return new TestRoot(wasm, plugin);
    };
  }

  void setupTest(std::string root_id = "", std::string vm_configuration = "",
                 envoy::extensions::wasm::v3::EnvironmentVariables envs = {}) {
    std::string code;
    if (std::get<0>(GetParam()) == "null") {
      code = "HttpWasmTestCpp";
    } else {
      if (std::get<1>(GetParam()) == "cpp") {
        code = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "test/extensions/filters/http/wasm/test_data/test_cpp.wasm"));
      } else {
        auto filename = !root_id.empty() ? root_id : vm_configuration;
        const auto basic_path = TestEnvironment::runfilesPath(
            absl::StrCat("test/extensions/filters/http/wasm/test_data/", filename));
        code = TestEnvironment::readFileToStringForTest(basic_path + "_rust.wasm");
      }
    }

    setRootId(root_id);
    setEnvs(envs);
    setVmConfiguration(vm_configuration);
    setupBase(std::get<0>(GetParam()), code, createContextFn());
  }

  void setupFilter() { setupFilterBase<TestFilter>(); }

  void setupGrpcStreamTest(Grpc::RawAsyncStreamCallbacks*& callbacks, std::string id);

  TestRoot& rootContext() { return *static_cast<TestRoot*>(root_context_); }
  TestFilter& filter() { return *static_cast<TestFilter*>(context_.get()); }

protected:
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  Grpc::MockAsyncClientManager async_client_manager_;
};

INSTANTIATE_TEST_SUITE_P(RuntimesAndLanguages, WasmHttpFilterTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_language_values);

// Bad code in initial config.
TEST_P(WasmHttpFilterTest, BadCode) {
  setupBase(std::get<0>(GetParam()), "bad code", createContextFn());
  EXPECT_EQ(wasm_, nullptr);
}

// Script touching headers only, request that is headers only.
TEST_P(WasmHttpFilterTest, HeadersOnlyRequestHeadersOnlyWithEnvVars) {
  envoy::extensions::wasm::v3::EnvironmentVariables envs;
  if (std::get<0>(GetParam()) != "null") {
    // Setup env vars.
    const std::string host_env_key = "ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV";
    const std::string host_env_value = "foo";
    const std::string env_key = "ENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV";
    const std::string env_value = "bar";
    TestEnvironment::setEnvVar(host_env_key, host_env_value, 0);
    envs.mutable_host_env_keys()->Add(host_env_key.c_str());
    (*envs.mutable_key_values())[env_key] = env_value;
  }
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
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, clearRouteCache()).Times(2);

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
  EXPECT_EQ(filter().encode100ContinueHeaders(response_headers),
            Http::FilterHeadersStatus::Continue);
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
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, clearRouteCache()).Times(2);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"server", "envoy"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  EXPECT_THAT(request_headers.get_("newheader"), Eq("newheadervalue"));
  EXPECT_THAT(request_headers.get_("server"), Eq("envoy-wasm"));
  Http::TestRequestTrailerMapImpl request_trailers{};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter().decodeTrailers(request_trailers));
  Http::MetadataMap request_metadata{};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter().decodeMetadata(request_metadata));

  // Verify that route cache is NOT cleared when modifying HTTP response headers.
  EXPECT_CALL(decoder_callbacks, clearRouteCache()).Times(0);

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
TEST_P(WasmHttpFilterTest, BodyRequestReadBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}, {"x-test-operation", "ReadBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that prepends and appends to the body.
TEST_P(WasmHttpFilterTest, BodyRequestPrependAndAppendToBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody prepend.hello.append"))));
  EXPECT_CALL(filter(), log_(spdlog::level::err,
                             Eq(absl::string_view("onBody prepend.prepend.hello.append.append"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "PrependAndAppendToBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  } else {
    // This status is not available in the rust SDK.
    // TODO: update all SDKs to the new revision of the spec and update the tests accordingly.
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter().encodeData(data, true));
  }
  filter().onDestroy();
}

// Script that replaces the body.
TEST_P(WasmHttpFilterTest, BodyRequestReplaceBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody replace")))).Times(2);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "ReplaceBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  if (std::get<1>(GetParam()) == "rust") {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data, true));
  } else {
    // This status is not available in the rust SDK.
    // TODO: update all SDKs to the new revision of the spec and update the tests accordingly.
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter().decodeData(data, true));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter().encodeData(data, true));
  }
  filter().onDestroy();
}

// Script that removes the body.
TEST_P(WasmHttpFilterTest, BodyRequestRemoveBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody "))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "RemoveBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that buffers the body.
TEST_P(WasmHttpFilterTest, BodyRequestBufferBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "BufferBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&bufferedBody));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .WillRepeatedly(Invoke([&bufferedBody](BufferFunction f) { f(bufferedBody); }));

  Buffer::OwnedImpl data1("hello");
  bufferedBody.add(data1);
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data1, false));

  Buffer::OwnedImpl data2(" again ");
  bufferedBody.add(data2);
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello again "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data2, false));

  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello again hello"))));
  Buffer::OwnedImpl data3("hello");
  bufferedBody.add(data3);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data3, true));

  // Verify that the response still works even though we buffered the request.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "ReadBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));
  // Should not buffer this time
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello")))).Times(2);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data1, true));

  filter().onDestroy();
}

// Script that prepends and appends to the buffered body.
TEST_P(WasmHttpFilterTest, BodyRequestPrependAndAppendToBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody prepend.hello.append"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"}, {"x-test-operation", "PrependAndAppendToBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that replaces the buffered body.
TEST_P(WasmHttpFilterTest, BodyRequestReplaceBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody replace"))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "ReplaceBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that removes the buffered body.
TEST_P(WasmHttpFilterTest, BodyRequestRemoveBufferedBody) {
  setupTest("body");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody "))));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"},
                                                 {"x-test-operation", "RemoveBufferedBody"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data, true));
  filter().onDestroy();
}

// Script that buffers the first part of the body and streams the rest
TEST_P(WasmHttpFilterTest, BodyRequestBufferThenStreamBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&bufferedBody));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .WillRepeatedly(Invoke([&bufferedBody](BufferFunction f) { f(bufferedBody); }));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "BufferTwoBodies"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data1, false));
  bufferedBody.add(data1);

  Buffer::OwnedImpl data2(", there, ");
  bufferedBody.add(data2);
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().decodeData(data2, false));

  // Previous callbacks returned "Buffer" so we have buffered so far
  Buffer::OwnedImpl data3("world!");
  bufferedBody.add(data3);
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, world!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data3, false));

  // Last callback returned "continue" so we just see individual chunks.
  Buffer::OwnedImpl data4("So it's ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody So it's "))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data4, false));

  Buffer::OwnedImpl data5("goodbye, then!");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody goodbye, then!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().decodeData(data5, true));

  filter().onDestroy();
}

// Script that buffers the first part of the body and streams the rest
TEST_P(WasmHttpFilterTest, BodyResponseBufferThenStreamBody) {
  setupTest("body");
  setupFilter();

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, false));

  Buffer::OwnedImpl bufferedBody;
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .WillRepeatedly(Invoke([&bufferedBody](BufferFunction f) { f(bufferedBody); }));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"x-test-operation", "BufferTwoBodies"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello"))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().encodeData(data1, false));
  bufferedBody.add(data1);

  Buffer::OwnedImpl data2(", there, ");
  bufferedBody.add(data2);
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, "))));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter().encodeData(data2, false));

  // Previous callbacks returned "Buffer" so we have buffered so far
  Buffer::OwnedImpl data3("world!");
  bufferedBody.add(data3);
  EXPECT_CALL(filter(),
              log_(spdlog::level::err, Eq(absl::string_view("onBody hello, there, world!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data3, false));

  // Last callback returned "continue" so we just see individual chunks.
  Buffer::OwnedImpl data4("So it's ");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody So it's "))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data4, false));

  Buffer::OwnedImpl data5("goodbye, then!");
  EXPECT_CALL(filter(), log_(spdlog::level::err, Eq(absl::string_view("onBody goodbye, then!"))));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter().encodeData(data5, true));

  filter().onDestroy();
}

// Script testing AccessLog::Instance::log.
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
  filter().log(&request_headers, &response_headers, &response_trailers, log_stream_info);
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
  filter().log(&request_headers, nullptr, nullptr, log_stream_info);
  filter().onDestroy();
}

TEST_P(WasmHttpFilterTest, AccessLogCreate) {
  setupTest("", "headers");
  setupFilter();
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onLog 2 / 200"))));
  EXPECT_CALL(filter(), log_(spdlog::level::warn, Eq(absl::string_view("onDone 2"))));

  StreamInfo::MockStreamInfo log_stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers{};
  filter().log(&request_headers, &response_headers, &response_trailers, log_stream_info);
  filter().onDestroy();
}

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
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "11"}}),
                      message->headers());
            EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(filter(), log_(spdlog::level::debug, Eq("response")));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq(":status -> 200")));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce(Invoke([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        response_message->body().add("response");
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      }));
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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "6"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce(Invoke([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
        NiceMock<Tracing::MockSpan> span;
        Http::TestResponseHeaderMapImpl response_header{{":status", "200"}};
        callbacks->onBeforeFinalizeUpstreamSpan(span, &response_header);
        callbacks->onSuccess(request, std::move(response_message));
        return proxy_wasm::WasmResult::Ok;
      }));
  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("continueRequest")));

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  filter().setDecoderFilterCallbacks(decoder_callbacks);
  EXPECT_CALL(decoder_callbacks, continueDecoding()).WillOnce(Invoke([&]() {
    // Verify that we're not resuming processing from within Wasm callback.
    EXPECT_EQ(proxy_wasm::current_context_, nullptr);
  }));

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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            return nullptr;
          }));

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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            Http::ResponseMessagePtr response(
                new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                    new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
            // Simulate code path for "no healthy host for HTTP connection pool" inline callback.
            cb.onSuccess(request, std::move(response));
            return nullptr;
          }));

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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "11"}}),
                      message->headers());
            EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(filter(), log_(spdlog::level::info, Eq("onRequestHeaders")))
      .WillOnce(Invoke([&](uint32_t, absl::string_view) -> proxy_wasm::WasmResult {
        callbacks->onFailure(request, Http::AsyncClient::FailureReason::Reset);
        return proxy_wasm::WasmResult::Ok;
      }));
  // TODO(PiotrSikora): RootContext handling is incomplete in the Rust SDK.
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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                      {":path", "/"},
                                                      {":authority", "foo"},
                                                      {"content-length", "11"}}),
                      message->headers());
            EXPECT_EQ((Http::TestRequestTrailerMapImpl{{"trail", "cow"}}), *message->trailers());
            callbacks = &cb;
            return &request;
          }));

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
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(
            Invoke([&](absl::string_view service_full_name, absl::string_view method_name,
                       Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                       Tracing::Span& span,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(service_full_name, "service");
              EXPECT_EQ(method_name, "method");
              ProtobufWkt::Value value;
              EXPECT_TRUE(
                  value.ParseFromArray(message->linearize(message->length()), message->length()));
              EXPECT_EQ(value.string_value(), "request");
              callbacks = &cb;
              parent_span = &span;
              EXPECT_EQ(options.timeout->count(), 1000);
              return &request;
            }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onCreateInitialMetadata(request_headers);
      callbacks->onSuccessRaw(std::move(response), span);
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallBadCall) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(Invoke([&](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                             Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                             const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
          return nullptr;
        }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter().decodeHeaders(request_headers, true));
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallFailure) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(
            Invoke([&](absl::string_view service_full_name, absl::string_view method_name,
                       Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                       Tracing::Span& span,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(service_full_name, "service");
              EXPECT_EQ(method_name, "method");
              ProtobufWkt::Value value;
              EXPECT_TRUE(
                  value.ParseFromArray(message->linearize(message->length()), message->length()));
              EXPECT_EQ(value.string_value(), "request");
              callbacks = &cb;
              parent_span = &span;
              EXPECT_EQ(options.timeout->count(), 1000);
              return &request;
            }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("failure bad")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    // Test some additional error paths.
    EXPECT_EQ(filter().grpcSend(99999, "", false), proxy_wasm::WasmResult::BadArgument);
    EXPECT_EQ(filter().grpcSend(10000, "", false), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcCancel(9999), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcCancel(10000), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcClose(9999), proxy_wasm::WasmResult::NotFound);
    EXPECT_EQ(filter().grpcClose(10000), proxy_wasm::WasmResult::NotFound);

    ProtobufWkt::Value value;
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
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(
            Invoke([&](absl::string_view service_full_name, absl::string_view method_name,
                       Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                       Tracing::Span& span,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(service_full_name, "service");
              EXPECT_EQ(method_name, "method");
              ProtobufWkt::Value value;
              EXPECT_TRUE(
                  value.ParseFromArray(message->linearize(message->length()), message->length()));
              EXPECT_EQ(value.string_value(), "request");
              callbacks = &cb;
              parent_span = &span;
              EXPECT_EQ(options.timeout->count(), 1000);
              return &request;
            }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    rootContext().onQueueReady(0);
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallClose) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(
            Invoke([&](absl::string_view service_full_name, absl::string_view method_name,
                       Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                       Tracing::Span& span,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(service_full_name, "service");
              EXPECT_EQ(method_name, "method");
              ProtobufWkt::Value value;
              EXPECT_TRUE(
                  value.ParseFromArray(message->linearize(message->length()), message->length()));
              EXPECT_EQ(value.string_value(), "request");
              callbacks = &cb;
              parent_span = &span;
              EXPECT_EQ(options.timeout->count(), 1000);
              return &request;
            }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    rootContext().onQueueReady(1);
  }
}

TEST_P(WasmHttpFilterTest, GrpcCallAfterDestroyed) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }
  std::array<std::string, 2> proto_or_cluster{"grpc_call_proto", "grpc_call"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    setupTest(id);
    setupFilter();

    if (id == "grpc_call_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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
        .WillOnce(
            Invoke([&](absl::string_view service_full_name, absl::string_view method_name,
                       Buffer::InstancePtr&& message, Grpc::RawAsyncRequestCallbacks& cb,
                       Tracing::Span& span,
                       const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(service_full_name, "service");
              EXPECT_EQ(method_name, "method");
              ProtobufWkt::Value value;
              EXPECT_TRUE(
                  value.ParseFromArray(message->linearize(message->length()), message->length()));
              EXPECT_EQ(value.string_value(), "request");
              callbacks = &cb;
              parent_span = &span;
              EXPECT_EQ(options.timeout->count(), 1000);
              return &request;
            }));
    EXPECT_CALL(*client_factory, create).WillOnce(Invoke([&]() -> Grpc::RawAsyncClientPtr {
      return std::move(async_client);
    }));
    EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
        .WillOnce(Invoke([&]() -> Grpc::AsyncClientManager& { return client_manager; }));
    EXPECT_CALL(client_manager, factoryForGrpcService(_, _, _))
        .WillOnce(
            Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
              return std::move(client_factory);
            }));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    EXPECT_CALL(request, cancel()).WillOnce([&]() { callbacks = nullptr; });

    // Destroy the Context, Plugin and VM.
    context_.reset();
    plugin_.reset();
    plugin_handle_.reset();
    wasm_.reset();

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_EQ(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      callbacks->onSuccessRaw(std::move(response), span);
    }
  }
}

void WasmHttpFilterTest::setupGrpcStreamTest(Grpc::RawAsyncStreamCallbacks*& callbacks,
                                             std::string id) {
  setupTest(id);
  setupFilter();

  EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, _))
      .WillRepeatedly(
          Invoke([&](const GrpcService&, Stats::Scope&, bool) -> Grpc::AsyncClientFactoryPtr {
            auto client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
            EXPECT_CALL(*client_factory, create)
                .WillRepeatedly(Invoke([&]() -> Grpc::RawAsyncClientPtr {
                  auto async_client = std::make_unique<Grpc::MockAsyncClient>();
                  EXPECT_CALL(*async_client, startRaw(_, _, _, _))
                      .WillRepeatedly(Invoke(
                          [&](absl::string_view service_full_name, absl::string_view method_name,
                              Grpc::RawAsyncStreamCallbacks& cb,
                              const Http::AsyncClient::StreamOptions&) -> Grpc::RawAsyncStream* {
                            EXPECT_EQ(service_full_name, "service");
                            if (method_name != "method") {
                              return nullptr;
                            }
                            callbacks = &cb;
                            return &async_stream_;
                          }));
                  return async_client;
                }));
            return client_factory;
          }));
  EXPECT_CALL(cluster_manager_, grpcAsyncClientManager())
      .WillRepeatedly(Invoke([&]() -> Grpc::AsyncClientManager& { return async_client_manager_; }));
}

TEST_P(WasmHttpFilterTest, GrpcStream) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }
  std::array<std::string, 2> proto_or_cluster{"grpc_stream_proto", "grpc_stream"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close done")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      Http::TestRequestHeaderMapImpl create_initial_metadata{{"test", "create_initial_metadata"}};
      callbacks->onCreateInitialMetadata(create_initial_metadata);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onReceiveTrailingMetadata(std::make_unique<Http::TestResponseTrailerMapImpl>());
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "done");
    }
  }
}

// Local close followed by remote close.
TEST_P(WasmHttpFilterTest, GrpcStreamCloseLocal) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }
  std::array<std::string, 2> proto_or_cluster{"grpc_stream_proto", "grpc_stream"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response close")));
    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close ok")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    ProtobufWkt::Value value;
    value.set_string_value("close");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      Http::TestRequestHeaderMapImpl create_initial_metadata{{"test", "create_initial_metadata"}};
      callbacks->onCreateInitialMetadata(create_initial_metadata);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "ok");
    }
  }
}

// Remote close followed by local close.
TEST_P(WasmHttpFilterTest, GrpcStreamCloseRemote) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_stream_proto", "grpc_stream"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("close close")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    if (callbacks) {
      Http::TestRequestHeaderMapImpl create_initial_metadata{{"test", "create_initial_metadata"}};
      callbacks->onCreateInitialMetadata(create_initial_metadata);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
      callbacks->onReceiveMessageRaw(std::move(response));
      callbacks->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "close");
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcStreamCancel) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_stream_proto", "grpc_stream"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
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

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      Http::TestRequestHeaderMapImpl create_initial_metadata{{"test", "create_initial_metadata"}};
      callbacks->onCreateInitialMetadata(create_initial_metadata);
      callbacks->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl{{"test", "reset"}}));
    }
  }
}

TEST_P(WasmHttpFilterTest, GrpcStreamOpenAtShutdown) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): gRPC call outs not yet supported in the Rust SDK.
    return;
  }

  std::array<std::string, 2> proto_or_cluster{"grpc_stream_proto", "grpc_stream"};
  for (const auto& id : proto_or_cluster) {
    TestScopedRuntime scoped_runtime;
    Grpc::RawAsyncStreamCallbacks* callbacks = nullptr;
    setupGrpcStreamTest(callbacks, id);

    if (id == "grpc_stream_proto") {
      Runtime::LoaderSingleton::getExisting()->mergeValues(
          {{"envoy.reloadable_features.wasm_cluster_name_envoy_grpc", "false"}});
    } else {
      cluster_manager_.initializeThreadLocalClusters({"cluster"});
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus service parse failure"))));
      EXPECT_CALL(filter(), log_(spdlog::level::err,
                                 Eq(absl::string_view("expected bogus method call failure"))));
      EXPECT_CALL(filter(),
                  log_(spdlog::level::err, Eq(absl::string_view("cluster call succeeded"))));
    }

    EXPECT_CALL(rootContext(), log_(spdlog::level::debug, Eq("response response")));
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
              filter().decodeHeaders(request_headers, false));

    ProtobufWkt::Value value;
    value.set_string_value("response");
    std::string response_string;
    EXPECT_TRUE(value.SerializeToString(&response_string));
    auto response = std::make_unique<Buffer::OwnedImpl>(response_string);
    EXPECT_NE(callbacks, nullptr);
    NiceMock<Tracing::MockSpan> span;
    if (callbacks) {
      Http::TestRequestHeaderMapImpl create_initial_metadata{{"test", "create_initial_metadata"}};
      callbacks->onCreateInitialMetadata(create_initial_metadata);
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
TEST_P(WasmHttpFilterTest, Metadata) {
#ifdef WIN32
  // TODO: re-enable this on Windows if and when the CEL `Antlr` parser compiles on Windows.
  GTEST_SKIP() << "Skipping on Windows";
#endif
  setupTest("", "metadata");
  setupFilter();
  envoy::config::core::v3::Node node_data;
  ProtobufWkt::Value node_val;
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
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
          HttpFilters::HttpFilterNames::get().Wasm,
          MessageUtil::keyValueStruct("wasm_request_get_key", "wasm_request_get_value")));

  rootContext().onTick(0);

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  absl::optional<std::chrono::nanoseconds> dur = std::chrono::nanoseconds(15000000);
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

  StreamInfo::MockStreamInfo log_stream_info;
  filter().log(&request_headers, nullptr, nullptr, log_stream_info);

  const auto& result =
      request_stream_info_.filterState()->getDataReadOnly<Filters::Common::Expr::CelState>(
          "wasm.wasm_request_set_key");
  EXPECT_EQ("wasm_request_set_value", result.value());

  filter().onDestroy();
  filter().onDestroy(); // Does nothing.
}

TEST_P(WasmHttpFilterTest, Property) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "property");
  setupFilter();
  envoy::config::core::v3::Node node_data;
  ProtobufWkt::Value node_val;
  node_val.set_string_value("sample_data");
  (*node_data.mutable_metadata()->mutable_fields())["istio.io/metadata"] = node_val;
  EXPECT_CALL(local_info_, node()).WillRepeatedly(ReturnRef(node_data));

  request_stream_info_.metadata_.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>(
          HttpFilters::HttpFilterNames::get().Wasm,
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
  StreamInfo::MockStreamInfo log_stream_info;
  request_stream_info_.route_name_ = "route12";
  request_stream_info_.requested_server_name_ = "w3.org";
  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(connection, id()).WillRepeatedly(Return(4));
  EXPECT_CALL(encoder_callbacks_, connection()).WillRepeatedly(Return(&connection));
  NiceMock<Router::MockRouteEntry> route_entry;
  EXPECT_CALL(request_stream_info_, routeEntry()).WillRepeatedly(Return(&route_entry));
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
  EXPECT_CALL(request_stream_info_, upstreamHost()).WillRepeatedly(Return(host_description));
  filter().log(&request_headers, nullptr, nullptr, log_stream_info);
}

TEST_P(WasmHttpFilterTest, ClusterMetadata) {
  if (std::get<1>(GetParam()) == "rust") {
    // TODO(PiotrSikora): test not yet implemented using Rust SDK.
    return;
  }
  setupTest("", "cluster_metadata");
  setupFilter();
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("cluster metadata: cluster"))));
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
  StreamInfo::MockStreamInfo log_stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(request_stream_info_));
  EXPECT_CALL(*cluster, metadata()).WillRepeatedly(ReturnRef(*cluster_metadata));
  EXPECT_CALL(*host_description, cluster()).WillRepeatedly(ReturnRef(*cluster));
  EXPECT_CALL(request_stream_info_, upstreamHost()).WillRepeatedly(Return(host_description));
  filter().log(&request_headers, nullptr, nullptr, log_stream_info);

  // If upstream host is empty, fallback to upstream cluster info for cluster metadata.
  EXPECT_CALL(request_stream_info_, upstreamHost()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(request_stream_info_, upstreamClusterInfo()).WillRepeatedly(Return(cluster));
  EXPECT_CALL(filter(),
              log_(spdlog::level::warn, Eq(absl::string_view("cluster metadata: cluster"))));
  filter().log(&request_headers, nullptr, nullptr, log_stream_info);
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

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
