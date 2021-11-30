#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"

using testing::ByMove;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {
namespace {

envoy_headers make_envoy_headers(std::vector<std::pair<std::string, std::string>> pairs) {
  envoy_map_entry* headers =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * pairs.size()));
  envoy_headers new_headers;
  new_headers.length = 0;
  new_headers.entries = headers;

  for (const auto& pair : pairs) {
    envoy_data key = Data::Utility::copyToBridgeData(pair.first);
    envoy_data value = Data::Utility::copyToBridgeData(pair.second);

    new_headers.entries[new_headers.length] = {key, value};
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

  struct CheckFilterState {
    std::string iteration_state, on_headers_called, headers_forwarded, on_data_called,
        data_forwarded, on_trailers_called, trailers_forwarded, on_resume_called, pending_headers,
        buffer, pending_trailers, stream_complete;
  };

  void checkFilterState(std::string name, std::string error_response, CheckFilterState request,
                        CheckFilterState response) {
    std::stringstream ss;
    filter_->dumpState(ss, 0);

    std::string expected_state_template =
        R"EOF(PlatformBridgeFilter, filter_name_: {}, error_response_: {}
  Request Filter, state_.iteration_state_: {}, state_.on_headers_called_: {}, state_.headers_forwarded_: {}, state_.on_data_called_: {}, state_.data_forwarded_: {}, state_.on_trailers_called_: {}, state_.trailers_forwarded_: {}, state_.on_resume_called_: {}, pending_headers_: {}, buffer: {}, pending_trailers_: {}, state_.stream_complete_: {}
  Response Filter, state_.iteration_state_: {}, state_.on_headers_called_: {}, state_.headers_forwarded_: {}, state_.on_data_called_: {}, state_.data_forwarded_: {}, state_.on_trailers_called_: {}, state_.trailers_forwarded_: {}, state_.on_resume_called_: {}, pending_headers_: {}, buffer: {}, pending_trailers_: {}, state_.stream_complete_: {}
)EOF";

    std::string expected_state = fmt::format(
        expected_state_template, name, error_response, request.iteration_state,
        request.on_headers_called, request.headers_forwarded, request.on_data_called,
        request.data_forwarded, request.on_trailers_called, request.trailers_forwarded,
        request.on_resume_called, request.pending_headers, request.buffer, request.pending_trailers,
        request.stream_complete, response.iteration_state, response.on_headers_called,
        response.headers_forwarded, response.on_data_called, response.data_forwarded,
        response.on_trailers_called, response.trailers_forwarded, response.on_resume_called,
        response.pending_headers, response.buffer, response.pending_trailers,
        response.stream_complete);

    EXPECT_EQ(ss.str(), expected_state);
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
    unsigned int on_cancel_calls;
    unsigned int on_error_calls;
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

  checkFilterState("NullImplementation", "0",
                   {"ongoing", "0", "1", "0", "1", "0", "1", "0", "null", "null", "null", "1"},
                   {"ongoing", "0", "1", "0", "1", "0", "1", "0", "null", "null", "null", "1"});

  filter_->onDestroy();

  free(null_filter);
}

TEST_F(PlatformBridgeFilterTest, PartialNullImplementation) {
  envoy_http_filter* noop_filter =
      static_cast<envoy_http_filter*>(safe_calloc(1, sizeof(envoy_http_filter)));
  filter_invocations invocations{};
  noop_filter->static_context = &invocations;
  noop_filter->init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "request body");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_request =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
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
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenResumeOnResumeDecodingWithData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_request =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
    EXPECT_EQ(pending_data, nullptr);
    EXPECT_EQ(pending_trailers, nullptr);
    EXPECT_FALSE(end_stream);
    invocations->on_resume_request_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":authority", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);

    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Data::Utility::toBridgeData(final_buffer);
    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, modified_data, nullptr};
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
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");
}

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenResumeOnResumeDecodingWithTrailers) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_request =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
    EXPECT_EQ(pending_data, nullptr);
    EXPECT_EQ(pending_trailers, nullptr);
    EXPECT_FALSE(end_stream);
    invocations->on_resume_request_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":authority", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);

    envoy_headers* modified_trailers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_trailers = make_envoy_headers({{"trailer", "test.trailer.async"}});

    return {kEnvoyFilterResumeStatusResumeIteration, modified_headers, nullptr, modified_trailers};
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
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).WillOnce(ReturnRef(trailers));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);

  EXPECT_FALSE(request_headers.get(Http::LowerCaseString("x-async-resumed")).empty());
  EXPECT_EQ(
      request_headers.get(Http::LowerCaseString("x-async-resumed"))[0]->value().getStringView(),
      "Very Yes");

  EXPECT_FALSE(trailers.get(Http::LowerCaseString("trailer")).empty());
  EXPECT_EQ(trailers.get(Http::LowerCaseString("trailer"))[0]->value().getStringView(),
            "test.trailer.async");
}

TEST_F(PlatformBridgeFilterTest, AsyncResumeDecodingIsNoopAfterPreviousResume) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "request body");
    EXPECT_TRUE(end_stream);
    invocations->on_request_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":authority", "test.code"}, {"content-length", "12"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };
  platform_filter.on_resume_request = [](envoy_headers*, envoy_data*, envoy_headers*, bool,
                                         envoy_stream_intel,
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "request body");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "AB", "ABC"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_request_data_calls++]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
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

TEST_F(PlatformBridgeFilterTest, BasicError) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool, envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->on_response_headers_calls++;
    ADD_FAILURE() << "on_headers should not get called for an error response.";
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->on_response_data_calls++;
    ADD_FAILURE() << "on_data should not get called for an error response.";
    release_envoy_data(c_data);
    return {kEnvoyFilterDataStatusStopIterationNoBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_error = [](envoy_error c_error, envoy_stream_intel, envoy_final_stream_intel,
                                const void* context) -> void {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->on_error_calls++;
    EXPECT_EQ(c_error.error_code, ENVOY_UNDEFINED_ERROR);
    EXPECT_EQ(Data::Utility::copyToString(c_error.message), "busted");
    EXPECT_EQ(c_error.attempt_count, 1);
    release_envoy_error(c_error);
  };

  setUpFilter(R"EOF(
platform_filter_name: BasicError
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{
      {"x-internal-error-code", "0"},
      {"x-internal-error-message", "busted"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("busted");

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_data, true));

  EXPECT_EQ(invocations.on_response_headers_calls, 0);
  EXPECT_EQ(invocations.on_response_data_calls, 0);
  EXPECT_EQ(invocations.on_error_calls, 1);
}

TEST_F(PlatformBridgeFilterTest, StopAndBufferThenResumeOnRequestData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_request_data_calls == 0) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "AB");
      EXPECT_FALSE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Data::Utility::toBridgeData(final_buffer);

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = nullptr;
    }

    invocations->on_request_data_calls++;
    release_envoy_data(c_data);
    return return_status;
  };

  Buffer::OwnedImpl decoding_buffer;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer())
      .Times(1)
      .WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
      .Times(1)
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_request_data_calls == 0) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "AB");
      EXPECT_TRUE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Data::Utility::toBridgeData(final_buffer);
      envoy_headers* modified_headers =
          static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
      *modified_headers =
          make_envoy_headers({{":authority", "test.code"}, {"content-length", "1"}});

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = modified_headers;
    }

    invocations->on_request_data_calls++;
    release_envoy_data(c_data);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "B", "C"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_request_data_calls++]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_request_data_calls]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");

    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Data::Utility::toBridgeData(final_buffer);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_request_data_calls]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");
    release_envoy_headers(c_trailers);
    invocations->on_request_trailers_calls++;
    return {kEnvoyFilterTrailersStatusStopIteration, envoy_noheaders, nullptr, nullptr};
  };
  platform_filter.on_resume_request =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
    EXPECT_EQ(Data::Utility::copyToString(*pending_data), "AB");
    EXPECT_EQ(pending_trailers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].value), "test trailer");
    EXPECT_TRUE(end_stream);

    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":authority", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Data::Utility::toBridgeData(final_buffer);
    release_envoy_data(*pending_data);
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
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
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

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenBufferThenDontResumeOnResumeDecoding) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_request_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                       const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_request_data_calls]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    invocations->on_request_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_request_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                           const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");
    release_envoy_headers(c_trailers);
    invocations->on_request_trailers_calls++;
    return {kEnvoyFilterTrailersStatusStopIteration, envoy_noheaders, nullptr, nullptr};
  };
  platform_filter.on_resume_request =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
    EXPECT_EQ(Data::Utility::copyToString(*pending_data), "AB");
    EXPECT_EQ(pending_trailers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].value), "test trailer");
    EXPECT_TRUE(end_stream);

    release_envoy_headers(*pending_headers);
    release_envoy_data(*pending_data);
    release_envoy_headers(*pending_trailers);

    invocations->on_resume_request_calls++;
    return {kEnvoyFilterResumeStatusStopIteration, nullptr, nullptr, nullptr};
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
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);
}

// DIVIDE

TEST_F(PlatformBridgeFilterTest, BasicContinueOnResponseHeaders) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "response body");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_response =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
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
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "response body");
    EXPECT_TRUE(end_stream);
    invocations->on_response_data_calls++;
    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "13"}});
    return {kEnvoyFilterDataStatusResumeIteration, c_data, modified_headers};
  };
  platform_filter.on_resume_response = [](envoy_headers*, envoy_data*, envoy_headers*, bool,
                                          envoy_stream_intel,
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

TEST_F(PlatformBridgeFilterTest, AsyncResumeEncodingIsNoopAfterFilterIsPendingDestruction) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_response = [](envoy_headers*, envoy_data*, envoy_headers*, bool,
                                          envoy_stream_intel,
                                          const void*) -> envoy_filter_resume_status {
    ADD_FAILURE() << "on_resume_response should not get called when filter is pending destruction.";
    return {kEnvoyFilterResumeStatusResumeIteration, nullptr, nullptr, nullptr};
  };
  platform_filter.release_filter = [](const void* context) -> void {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->release_filter_calls++;
  };

  setUpFilter(R"EOF(
platform_filter_name: AsyncResumeEncodingIsNoopAfterFilterIsPendingDestruction
)EOF",
              &platform_filter);
  EXPECT_EQ(invocations.init_filter_calls, 1);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(invocations.on_response_headers_calls, 1);

  Buffer::OwnedImpl response_data = Buffer::OwnedImpl("response body");

  // Simulate posted resume call.
  Event::PostCb resume_post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&resume_post_cb));
  EXPECT_CALL(encoder_callbacks_, continueEncoding()).Times(0);
  filter_->resumeEncoding();

  // Simulate pending destruction.
  filter_->onDestroy();
  EXPECT_EQ(invocations.release_filter_calls, 1);

  // Execute late resume callback.
  resume_post_cb();

  // Assert late resume attempt was a no-op.
  EXPECT_EQ(invocations.on_resume_response_calls, 0);
}

TEST_F(PlatformBridgeFilterTest, BasicContinueOnResponseData) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(Data::Utility::copyToString(c_data), "response body");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "AB", "ABC"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_response_data_calls++]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_response_data_calls == 0) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "AB");
      EXPECT_FALSE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Data::Utility::toBridgeData(final_buffer);

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = nullptr;
    }

    invocations->on_response_data_calls++;
    release_envoy_data(c_data);
    return return_status;
  };

  Buffer::OwnedImpl encoding_buffer;
  EXPECT_CALL(encoder_callbacks_, encodingBuffer())
      .Times(1)
      .WillRepeatedly(Return(&encoding_buffer));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
      .Times(1)
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    envoy_filter_data_status return_status;

    if (invocations->on_response_data_calls == 0) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "A");
      EXPECT_FALSE(end_stream);

      return_status.status = kEnvoyFilterDataStatusStopIterationAndBuffer;
      return_status.data = envoy_nodata;
      return_status.pending_headers = nullptr;
    } else {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "AB");
      EXPECT_TRUE(end_stream);
      Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
      envoy_data final_data = Data::Utility::toBridgeData(final_buffer);
      envoy_headers* modified_headers =
          static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
      *modified_headers = make_envoy_headers({{":status", "test.code"}, {"content-length", "1"}});

      return_status.status = kEnvoyFilterDataStatusResumeIteration;
      return_status.data = final_data;
      return_status.pending_headers = modified_headers;
    }

    invocations->on_response_data_calls++;
    release_envoy_data(c_data);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[3] = {"A", "B", "C"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_response_data_calls++]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_response_data_calls]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    invocations->on_response_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");

    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Data::Utility::toBridgeData(final_buffer);
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
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_response_headers = [](envoy_headers c_headers, bool end_stream,
                                           envoy_stream_intel,
                                           const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_response_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_response_data = [](envoy_data c_data, bool end_stream, envoy_stream_intel,
                                        const void* context) -> envoy_filter_data_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    std::string expected_data[2] = {"A", "AB"};
    EXPECT_EQ(Data::Utility::copyToString(c_data),
              expected_data[invocations->on_response_data_calls]);
    EXPECT_FALSE(end_stream);
    release_envoy_data(c_data);
    invocations->on_response_data_calls++;
    return {kEnvoyFilterDataStatusStopIterationAndBuffer, envoy_nodata, nullptr};
  };
  platform_filter.on_response_trailers = [](envoy_headers c_trailers, envoy_stream_intel,
                                            const void* context) -> envoy_filter_trailers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_trailers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(c_trailers.entries[0].value), "test trailer");
    release_envoy_headers(c_trailers);
    invocations->on_response_trailers_calls++;
    return {kEnvoyFilterTrailersStatusStopIteration, envoy_noheaders, nullptr, nullptr};
  };
  platform_filter.on_resume_response =
      [](envoy_headers* pending_headers, envoy_data* pending_data, envoy_headers* pending_trailers,
         bool end_stream, envoy_stream_intel, const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(pending_headers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].key), ":status");
    EXPECT_EQ(Data::Utility::copyToString(pending_headers->entries[0].value), "test.code");
    EXPECT_EQ(Data::Utility::copyToString(*pending_data), "AB");
    EXPECT_EQ(pending_trailers->length, 1);
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].key), "x-test-trailer");
    EXPECT_EQ(Data::Utility::copyToString(pending_trailers->entries[0].value), "test trailer");
    EXPECT_TRUE(end_stream);

    envoy_headers* modified_headers =
        static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_headers)));
    *modified_headers =
        make_envoy_headers({{":status", "test.code"}, {"x-async-resumed", "Very Yes"}});
    release_envoy_headers(*pending_headers);
    Buffer::OwnedImpl final_buffer = Buffer::OwnedImpl("C");
    envoy_data* modified_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_data)));
    *modified_data = Data::Utility::toBridgeData(final_buffer);
    release_envoy_data(*pending_data);
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
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
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

TEST_F(PlatformBridgeFilterTest, StopOnRequestHeadersThenResumeOnResumeDecodingPassthrough) {
  envoy_http_filter platform_filter{};
  filter_invocations invocations{};
  platform_filter.static_context = &invocations;
  platform_filter.init_filter = [](const void* context) -> const void* {
    envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
    filter_invocations* invocations =
        static_cast<filter_invocations*>(const_cast<void*>(c_filter->static_context));
    invocations->init_filter_calls++;
    return invocations;
  };
  platform_filter.on_request_headers = [](envoy_headers c_headers, bool end_stream,
                                          envoy_stream_intel,
                                          const void* context) -> envoy_filter_headers_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    EXPECT_EQ(c_headers.length, 1);
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].key), ":authority");
    EXPECT_EQ(Data::Utility::copyToString(c_headers.entries[0].value), "test.code");
    EXPECT_FALSE(end_stream);
    invocations->on_request_headers_calls++;
    release_envoy_headers(c_headers);
    return {kEnvoyFilterHeadersStatusStopIteration, envoy_noheaders};
  };
  platform_filter.on_resume_request = [](envoy_headers* pending_headers, envoy_data*,
                                         envoy_headers*, bool, envoy_stream_intel,
                                         const void* context) -> envoy_filter_resume_status {
    filter_invocations* invocations = static_cast<filter_invocations*>(const_cast<void*>(context));
    invocations->on_resume_request_calls++;
    return {kEnvoyFilterResumeStatusResumeIteration, pending_headers, nullptr, nullptr};
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
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  filter_->resumeDecoding();
  resume_post_cb();
  EXPECT_EQ(invocations.on_resume_request_calls, 1);
}

} // namespace
} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
