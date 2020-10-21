#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/buffer/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"

using testing::ByMove;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {
namespace {

std::string to_string(envoy_data data) {
  return std::string(reinterpret_cast<const char*>(data.bytes), data.length);
}

envoy_data make_envoy_data(const std::string& s) {
  return copy_envoy_data(s.size(), reinterpret_cast<const uint8_t*>(s.c_str()));
}

envoy_headers make_envoy_headers(std::vector<std::pair<std::string, std::string>> pairs) {
  envoy_header* headers =
      static_cast<envoy_header*>(safe_malloc(sizeof(envoy_header) * pairs.size()));
  envoy_headers new_headers;
  new_headers.length = 0;
  new_headers.headers = headers;

  for (const auto& pair : pairs) {
    envoy_data key = make_envoy_data(pair.first);
    envoy_data value = make_envoy_data(pair.second);

    new_headers.headers[new_headers.length] = {key, value};
    new_headers.length++;
  }

  return new_headers;
}

class PlatformBridgeFilterTest : public testing::Test {
public:
  void setUpFilter(std::string&& yaml, envoy_http_filter* platform_filter) {
    envoymobile::extensions::filters::http::platform_bridge::PlatformBridge config;
    TestUtility::loadFromYaml(yaml, config);
    Api::External::registerApi(config.platform_filter_name(), platform_filter);

    config_ = std::make_shared<PlatformBridgeFilterConfig>(config);
    filter_ = std::make_shared<PlatformBridgeFilter>(config_, dispatcher_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  typedef struct {
    unsigned int init_filter_calls;
    unsigned int on_request_headers_calls;
    unsigned int on_request_data_calls;
    unsigned int on_request_trailers_calls;
    unsigned int on_response_headers_calls;
    unsigned int on_response_data_calls;
    unsigned int on_response_trailers_calls;
    unsigned int set_request_callbacks_calls;
    unsigned int on_resume_request_calls;
    unsigned int set_response_callbacks_calls;
    unsigned int on_resume_response_calls;
    unsigned int release_filter_calls;
  } filter_invocations;

  PlatformBridgeFilterConfigSharedPtr config_;
  PlatformBridgeFilterSharedPtr filter_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(PlatformBridgeFilterTest, NullImplementation) {
  envoy_http_filter* null_filter =
      static_cast<envoy_http_filter*>(safe_calloc(1, sizeof(envoy_http_filter)));
  setUpFilter("platform_filter_name: NullImplementation\n", null_filter);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "test trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));

  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));

  filter_->onDestroy();

  free(null_filter);
}

TEST_F(PlatformBridgeFilterTest, PartialNullImplementation) {
  envoy_http_filter* noop_filter =
      static_cast<envoy_http_filter*>(safe_calloc(1, sizeof(envoy_http_filter)));
  filter_invocations invocations{};
  noop_filter->static_context = &invocations;
  noop_filter->init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  noop_filter->release_filter = [](const void* context) -> void {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->release_filter_calls++;
  };
  setUpFilter("platform_filter_name: PartialNullImplementation\n", noop_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "test trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, false));

  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));

  filter_->onDestroy();
  EXPECT_EQ(invocations.release_filter_calls, 1);

  free(noop_filter);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnRequestHeaders) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_TRUE(end_stream);
    invocations->on_request_headers_calls++;
    return {kEnvoyFilterHeadersStatusContinue, c_headers};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnRequestHeaders
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenResumeOnData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "request body");
    EXPECT_TRUE(end_stream);
    invocations->on_request_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":authority", "test.code"}, {"content-length", "12"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopOnRequestHeadersThenResumeOnData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_data, true));
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "12");
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenResumeOnResumeDecoding) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_request = [](envoy_headers* pending_headers, envoy_data* pending_data,
                                         envoy_headers* pending_trailers, bool end_stream,
                                         const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(to_string(pending_headers->headers[0].key), ":authority");
    EXPECT_EQ(to_string(pending_headers->headers[0].value), "test.code");
    EXPECT_EQ(pending_data, nullptr);
    EXPECT_EQ(pending_trailers, nullptr);
    EXPECT_FALSE(end_stream);
    invocations->on_resume_request_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":authority", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopOnRequestHeadersThenResumeOnResumeDecoding
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(1);
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");
}

TEST_F(PlatformBridgeFilterTest, AsyncResumeDecodingIsNoopAfterPreviousResume) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "request body");
    EXPECT_TRUE(end_stream);
    invocations->on_request_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":authority", "test.code"}, {"content-length", "12"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };
  platform_filter.on_resume_request = [](envoy_headers*, envoy_data*, envoy_headers*, bool,
                                         const void*) -> envoy_filter_resume_status {
    ADD_FAILURE() << "on_resume_request should not get called when iteration is already ongoing.";
    return {kEnvoyFilterResumeStatusResumeIteration, nullptr, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: AsyncResumeDecodingIsNoopAfterPreviousResume
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_data, true));
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "12");

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 0);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnRequestData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "request body");
    EXPECT_TRUE(end_stream);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusContinue, c_data, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnRequestData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_data, true));
  EXPECT_EQ(invocations.on_request_data_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopAndBufferOnRequestData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "AB", "ABC"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_request_data_calls++]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(3)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(decoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopAndBufferOnRequestData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  decoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 2);

  Buffer::OwnedImpl third_chunk = Buffer::OwnedImpl("C");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(third_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 3);
}

TEST_F(PlatformBridgeFilterTest, StopAndBufferThenResumeOnRequestData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_request_data_calls == 0) {
      EXPECT_EQ(to_string(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(to_string(c_data), "AB");
      EXPECT_FALSE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Buffer::Utility::toBridgeData(final_buffer);

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = nullptr;
    }

    invocations->on_request_data_calls++;
    c_data.release(c_data.context);
    return return_status;
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(2)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(decoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopAndBufferThenResumeOnRequestData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  decoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 2);
  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(decoding_buffer.toString(), "C");
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenBufferThenResumeOnData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_request_data_calls == 0) {
      EXPECT_EQ(to_string(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(to_string(c_data), "AB");
      EXPECT_TRUE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Buffer::Utility::toBridgeData(final_buffer);
      envoy_headers* modified_headers =
          static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
      *modified_headers =
          make_envoy_headers({{":authority", "test.code"}, {"content-length", "1"}});

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = modified_headers;
    }

    invocations->on_request_data_calls++;
    c_data.release(c_data.context);
    return return_status;
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(2)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(decoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnRequestHeadersThenBufferThenResumeOnData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  decoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(second_chunk, true));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 2);
  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(decoding_buffer.toString(), "C");

  // Pending headers have been updated with value from ResumeIteration.
  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "1");
}

TEST_F(PlatformBridgeFilterTest, StopNoBufferOnRequestData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "B", "C"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_request_data_calls++]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    return {kEnvoyFilterDataStatusStopIterationNoBuffer, envoy_nodata, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopNoBufferOnRequestData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(first_chunk, false));
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(second_chunk, false));
  EXPECT_EQ(invocations.on_request_data_calls, 2);

  Buffer::OwnedImpl third_chunk = Buffer::OwnedImpl("C");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(third_chunk, false));
  EXPECT_EQ(invocations.on_request_data_calls, 3);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnRequestTrailers) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");
    invocations->on_request_trailers_calls++;
    return {kEnvoyFilterTrailersStatusContinue, c_trailers, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnRequestTrailers
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(invocations.on_request_trailers_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenBufferThenResumeOnTrailers) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_request_data_calls]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");

    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Buffer::Utility::toBridgeData(final_buffer);
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":authority", "test.code"}, {"content-length", "1"}});

    invocations->on_request_trailers_calls++;
    return {kEnvoyFilterTrailersStatusResumeIteration, c_trailers, modified_headers, modified_data};
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(3)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(decoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnRequestHeadersThenBufferThenResumeOnTrailers
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  decoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 2);
  EXPECT_EQ(decoding_buffer.toString(), "AB");

  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(invocations.on_request_trailers_calls, 1);

  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(decoding_buffer.toString(), "C");

  // Pending headers have been updated with value from ResumeIteration.
  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "1");
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenBufferThenResumeOnResumeDecoding) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":authority");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_request_data_calls]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");
    release_envoy_headers(c_trailers);
    invocations->on_request_trailers_calls++;
    return {kEnvoyFilterTrailersStatusStopIteration, envoy_noheaders, nullptr, nullptr};
  };
  platform_filter.on_resume_request = [](envoy_headers* pending_headers, envoy_data* pending_data,
                                         envoy_headers* pending_trailers, bool end_stream,
                                         const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(to_string(pending_headers->headers[0].key), ":authority");
    EXPECT_EQ(to_string(pending_headers->headers[0].value), "test.code");
    EXPECT_EQ(to_string(*pending_data), "AB");
    EXPECT_EQ(pending_trailers->length, 1);
    EXPECT_EQ(to_string(pending_trailers->headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(pending_trailers->headers[0].value), "test trailer");
    EXPECT_TRUE(end_stream);

    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":authority", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Buffer::Utility::toBridgeData(final_buffer);
    pending_data->release(pending_data->context);
    envoy_headers* modified_trailers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_trailers =
        make_envoy_headers({{"x-test-trailer", "test trailer"}, {"x-async-resumed", "yes"}});
    release_envoy_headers(*pending_trailers);

    invocations->on_resume_request_calls++;
    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, modified_data,
            modified_trailers};
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(4)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(4)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(decoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnRequestHeadersThenBufferThenResumeOnResumeDecoding
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(invocations.on_request_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  decoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_request_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_request_data_calls, 2);
  EXPECT_EQ(decoding_buffer.toString(), "AB");

  Http::TestRequestTrailerMapImpl request_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(invocations.on_request_trailers_calls, 1);

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(1);
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);

  // Pending headers have been updated with the value from ResumeIteration.
  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");

  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(decoding_buffer.toString(), "C");

  // Pending trailers have been updated with value from ResumeIteration.
  EXPECT_FALSE(request_trailers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_trailers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "yes");
}

// DIVIDE

TEST_F(PlatformBridgeFilterTest, BasicContinueOnResponseHeaders) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_TRUE(end_stream);
    invocations->on_response_headers_calls++;
    return {kEnvoyFilterHeadersStatusContinue, c_headers};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnResponseHeaders
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopOnResponseHeadersThenResumeOnData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "response body");
    EXPECT_TRUE(end_stream);
    invocations->on_response_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "13"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopOnResponseHeadersThenResumeOnData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "13");
}

TEST_F(PlatformBridgeFilterTest, StopOnResponseHeadersThenResumeOnResumeEncoding) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_response = [](envoy_headers* pending_headers, envoy_data* pending_data,
                                          envoy_headers* pending_trailers, bool end_stream,
                                          const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(to_string(pending_headers->headers[0].key), ":status");
    EXPECT_EQ(to_string(pending_headers->headers[0].value), "test.code");
    EXPECT_EQ(pending_data, nullptr);
    EXPECT_EQ(pending_trailers, nullptr);
    EXPECT_FALSE(end_stream);
    invocations->on_resume_response_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":status", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopOnResponseHeadersThenResumeOnResumeEncoding
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(encoder_callbacks_, continueEncoding()).Times(1);
  filter_->resumeEncoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_response_calls, 1);

  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");
}

TEST_F(PlatformBridgeFilterTest, AsyncResumeEncodingIsNoopAfterPreviousResume) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "response body");
    EXPECT_TRUE(end_stream);
    invocations->on_response_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "13"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };
  platform_filter.on_resume_response = [](envoy_headers*, envoy_data*, envoy_headers*, bool,
                                          const void*) -> envoy_filter_resume_status {
    ADD_FAILURE() << "on_resume_response should not get called when iteration is already ongoing.";
    return {kEnvoyFilterResumeStatusResumeIteration, nullptr, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: AsyncResumeEncodingIsNoopAfterPreviousResume
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "13");

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(encoder_callbacks_, continueEncoding()).Times(0);
  filter_->resumeEncoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_response_calls, 0);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnResponseData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(to_string(c_data), "response body");
    EXPECT_TRUE(end_stream);
    invocations->on_response_data_calls++;
    return {kEnvoyFilterDataStatusContinue, c_data, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnResponseData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));
  EXPECT_EQ(invocations.on_response_data_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopAndBufferOnResponseData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "AB", "ABC"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_response_data_calls++]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(3)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(encoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopAndBufferOnResponseData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  encoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 2);

  Buffer::OwnedImpl third_chunk = Buffer::OwnedImpl("C");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(third_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 3);
}

TEST_F(PlatformBridgeFilterTest, StopAndBufferThenResumeOnResponseData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_response_data_calls == 0) {
      EXPECT_EQ(to_string(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(to_string(c_data), "AB");
      EXPECT_FALSE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Buffer::Utility::toBridgeData(final_buffer);

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = nullptr;
    }

    invocations->on_response_data_calls++;
    c_data.release(c_data.context);
    return return_status;
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(2)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(encoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopAndBufferThenResumeOnResponseData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  encoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 2);
  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(encoding_buffer.toString(), "C");
}

TEST_F(PlatformBridgeFilterTest, StopOnResponseHeadersThenBufferThenResumeOnData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_response_data_calls == 0) {
      EXPECT_EQ(to_string(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(to_string(c_data), "AB");
      EXPECT_TRUE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Buffer::Utility::toBridgeData(final_buffer);
      envoy_headers* modified_headers =
          static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
      *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "1"}});

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = modified_headers;
    }

    invocations->on_response_data_calls++;
    c_data.release(c_data.context);
    return return_status;
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(2)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(encoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnResponseHeadersThenBufferThenResumeOnData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  encoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(second_chunk, true));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 2);
  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(encoding_buffer.toString(), "C");

  // Pending headers have been updated with value from ResumeIteration.
  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "1");
}

TEST_F(PlatformBridgeFilterTest, StopNoBufferOnResponseData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "B", "C"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_response_data_calls++]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    return {kEnvoyFilterDataStatusStopIterationNoBuffer, envoy_nodata, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: StopNoBufferOnResponseData
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(first_chunk, false));
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(second_chunk, false));
  EXPECT_EQ(invocations.on_response_data_calls, 2);

  Buffer::OwnedImpl third_chunk = Buffer::OwnedImpl("C");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(third_chunk, false));
  EXPECT_EQ(invocations.on_response_data_calls, 3);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnResponseTrailers) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");
    invocations->on_response_trailers_calls++;
    return {kEnvoyFilterTrailersStatusContinue, c_trailers, nullptr, nullptr};
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicContinueOnResponseTrailers
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(invocations.on_response_trailers_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopOnResponseHeadersThenBufferThenResumeOnTrailers) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_response_data_calls]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    invocations->on_response_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");

    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Buffer::Utility::toBridgeData(final_buffer);
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "1"}});

    invocations->on_response_trailers_calls++;
    return {kEnvoyFilterTrailersStatusResumeIteration, c_trailers, modified_headers, modified_data};
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(3)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(encoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnResponseHeadersThenBufferThenResumeOnTrailers
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  encoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 2);
  EXPECT_EQ(encoding_buffer.toString(), "AB");

  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(invocations.on_response_trailers_calls, 1);

  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(encoding_buffer.toString(), "C");

  // Pending headers have been updated with value from ResumeIteration.
  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("content-length")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
      "1");
}

TEST_F(PlatformBridgeFilterTest, StopOnResponseHeadersThenBufferThenResumeOnResumeEncoding) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->init_filter_calls++;
    return context;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(to_string(c_headers.headers[0].key), ":status");
    EXPECT_EQ(to_string(c_headers.headers[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(to_string(c_data), expected_data[invocations->on_response_data_calls]);
    EXPECT_FALSE(end_stream);
    c_data.release(c_data.context);
    invocations->on_response_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(to_string(c_trailers.headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(c_trailers.headers[0].value), "test trailer");
    release_envoy_headers(c_trailers);
    invocations->on_response_trailers_calls++;
    return {kEnvoyFilterTrailersStatusStopIteration, envoy_noheaders, nullptr, nullptr};
  };
  platform_filter.on_resume_response = [](envoy_headers* pending_headers, envoy_data* pending_data,
                                          envoy_headers* pending_trailers, bool end_stream,
                                          const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(to_string(pending_headers->headers[0].key), ":status");
    EXPECT_EQ(to_string(pending_headers->headers[0].value), "test.code");
    EXPECT_EQ(to_string(*pending_data), "AB");
    EXPECT_EQ(pending_trailers->length, 1);
    EXPECT_EQ(to_string(pending_trailers->headers[0].key), "x-test-trailer");
    EXPECT_EQ(to_string(pending_trailers->headers[0].value), "test trailer");
    EXPECT_TRUE(end_stream);

    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":status", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Buffer::Utility::toBridgeData(final_buffer);
    pending_data->release(pending_data->context);
    envoy_headers* modified_trailers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_trailers =
        make_envoy_headers({{"x-test-trailer", "test trailer"}, {"x-async-resumed", "yes"}});
    release_envoy_headers(*pending_trailers);

    invocations->on_resume_response_calls++;
    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, modified_data,
            modified_trailers};
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(4)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(4)
      .WillRepeatedly(Invoke([&](std::function<void(Buffer::Instance&)> callback) -> void {
        callback(encoding_buffer);
      }));

  setUpFilter(R"EOF(
platform_filter_name: StopOnResponseHeadersThenBufferThenResumeOnResumeEncoding
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl first_chunk = Buffer::OwnedImpl("A");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->encodeData(first_chunk, false));
  // Since the return code can't be handled in a unit test, manually update the buffer here.
  encoding_buffer.move(first_chunk);
  EXPECT_EQ(invocations.on_response_data_calls, 1);

  Buffer::OwnedImpl second_chunk = Buffer::OwnedImpl("B");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(second_chunk, false));
  // Manual update not required, because once iteration is stopped, data is added directly.
  EXPECT_EQ(invocations.on_response_data_calls, 2);
  EXPECT_EQ(encoding_buffer.toString(), "AB");

  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-trailer", "test trailer"}};

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(invocations.on_response_trailers_calls, 1);

  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(encoder_callbacks_, continueEncoding()).Times(1);
  filter_->resumeEncoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_response_calls, 1);

  // Pending headers have been updated with the value from ResumeIteration.
  EXPECT_FALSE(response_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      response_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");

  // Buffer has been updated with value from ResumeIteration.
  EXPECT_EQ(encoding_buffer.toString(), "C");

  // Pending trailers have been updated with value from ResumeIteration.
  EXPECT_FALSE(response_trailers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      response_trailers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "yes");
}

} // namespace
} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
