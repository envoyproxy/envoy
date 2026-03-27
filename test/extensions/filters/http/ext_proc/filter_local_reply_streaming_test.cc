#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"
#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response.h"
#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response_factory.h"

#include "test/extensions/filters/http/ext_proc/filter_test_common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::ProcessingResponse;
using ::envoy::service::ext_proc::v3::StreamedBodyResponse;

using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterFactoryCb;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::RequestHeaderMapPtr;
using ::Envoy::Http::ResponseHeaderMapPtr;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;

using ::testing::Eq;
using ::testing::Unused;

void makeLocalResponseHeaders(HttpHeaders& header_resp, bool end_of_stream) {
  auto header = header_resp.mutable_headers()->add_headers();
  header->set_key(":status");
  header->set_raw_value("200");
  header = header_resp.mutable_headers()->add_headers();
  header->set_key("x-some-other-header");
  header->set_raw_value("no");
  header_resp.set_end_of_stream(end_of_stream);
}

void makeLocalResponseDuplexBody(StreamedBodyResponse& body_resp, absl::string_view body,
                                 bool end_of_stream) {
  body_resp.set_body(body);
  body_resp.set_end_of_stream(end_of_stream);
}

void makeLocalResponseTrailers(envoy::config::core::v3::HeaderMap& trailers_resp) {
  auto add = trailers_resp.add_headers();
  add->set_key("x-some-other-trailer");
  add->set_raw_value("no");
}

// Test suite for failure_mode_allow override.
class StreamingLocalReplyTest : public HttpFilterTest {
public:
  void processRequestHeadersAndStartLocalResponse(
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HttpHeaders&)>>
          cb) {
    ASSERT_TRUE(last_request_.has_request_headers());
    const auto& headers = last_request_.request_headers();

    auto response = std::make_unique<ProcessingResponse>();
    auto* headers_response =
        response->mutable_streamed_immediate_response()->mutable_headers_response();
    if (cb) {
      (*cb)(headers, *response, *headers_response);
    }

    if (observability_mode_) {
      EXPECT_TRUE(last_request_.observability_mode());
      return;
    }

    EXPECT_FALSE(last_request_.observability_mode());
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void processRequestBodyAndSendLocalResponse(
      absl::optional<
          std::function<void(const HttpBody&, ProcessingResponse&, StreamedBodyResponse&)>>
          cb) {
    ASSERT_TRUE(last_request_.has_request_body());
    const auto& body = last_request_.request_body();

    auto response = std::make_unique<ProcessingResponse>();
    auto* body_response = response->mutable_streamed_immediate_response()->mutable_body_response();
    if (cb) {
      (*cb)(body, *response, *body_response);
    }

    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void processRequestTrailersAndSendLocalResponse(
      absl::optional<std::function<void(const HttpTrailers&, ProcessingResponse&,
                                        envoy::config::core::v3::HeaderMap&)>>
          cb) {
    ASSERT_TRUE(last_request_.has_request_trailers());
    const auto& trailers = last_request_.request_trailers();

    auto response = std::make_unique<ProcessingResponse>();
    auto* trailers_response =
        response->mutable_streamed_immediate_response()->mutable_trailers_response();
    if (cb) {
      (*cb)(trailers, *response, *trailers_response);
    }

    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void sendStreamingLocalResponseBody(
      absl::optional<std::function<void(ProcessingResponse&, StreamedBodyResponse&)>> cb) {
    auto response = std::make_unique<ProcessingResponse>();
    auto* body_response = response->mutable_streamed_immediate_response()->mutable_body_response();
    if (cb) {
      (*cb)(*response, *body_response);
    }
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void sendStreamingLocalResponseTrailers(
      absl::optional<std::function<void(ProcessingResponse&, envoy::config::core::v3::HeaderMap&)>>
          cb) {
    auto response = std::make_unique<ProcessingResponse>();
    auto* trailers_response =
        response->mutable_streamed_immediate_response()->mutable_trailers_response();
    if (cb) {
      (*cb)(*response, *trailers_response);
    }
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void expectFailureWithWrongBodyMode(absl::string_view body_mode,
                                      absl::string_view error_details) {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
    std::string config = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: "ext_proc_server"
    processing_mode:
      request_header_mode: "SEND"
      request_body_mode: "$0"
    )EOF";
    config = absl::Substitute(config, body_mode);
    initialize(std::move(config));

    // Decoding should not be continued since ext_proc is the terminal filter for local responses.
    EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
    // In local reply mode no data should be injected back into deciding chain.
    EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
    EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

    EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                   Eq(absl::nullopt), error_details));
    // Indicate that local response body is expected. Request should fail as the body send mode
    // is BUFFERED.
    processRequestHeadersAndStartLocalResponse(
        [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
          makeLocalResponseHeaders(header_resp, false);
        });

    filter_->onDestroy();

    EXPECT_EQ(1, config_->stats().streams_started_.value());
    EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
    EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
    EXPECT_EQ(1, config_->stats().streams_closed_.value());
  }
};

TEST_F(StreamingLocalReplyTest, LocalHeadersOnlyRequestAndResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalHeadersOnlyRequestAndResponseWithBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        // Indicate to the client that there is body to follow.
        makeLocalResponseHeaders(header_resp, false);
      });

  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalHeadersOnlyRequestAndResponseWithBodyInMultipleChunks) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        // Indicate to the client that there is body to follow.
        makeLocalResponseHeaders(header_resp, false);
      });

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false)).Times(2);
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  for (int i = 0; i < 3; ++i) {
    sendStreamingLocalResponseBody(
        [&i](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
          makeLocalResponseDuplexBody(body_resp, "local response body", i == 2);
        });
  }

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalHeadersOnlyRequestAndResponseWithBodyAndTrailers) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        // Indicate to the client that there is body to follow.
        makeLocalResponseHeaders(header_resp, false);
      });

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", false);
  });

  Envoy::Http::TestResponseHeaderMapImpl expected_trailers{{"x-some-other-trailer", "no"}};
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(HeaderMapEqualRef(&expected_trailers)));
  sendStreamingLocalResponseTrailers(
      [](const ProcessingResponse&, envoy::config::core::v3::HeaderMap& trailers_resp) {
        makeLocalResponseTrailers(trailers_resp);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithBodySkipAndLocalHeadersOnlyResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "NONE"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");
  // At this point it is not known if ext_proc will initiate a local response or not. So the filter
  // will buffer the data while waiting for the response headers.
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithBodySkipAndLocalResponseWithBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "NONE"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");
  // At this point it is not known if ext_proc will initiate a local response or not. So the filter
  // will buffer the data while waiting for the response headers.
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  // At this point Envoy knows that local response had started and the request
  // body should be discarded since it does not need to be sent to ext_proc server.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithBodyAndTrailersSkipAndLocalResponseWithBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "NONE"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");
  // At this point it is not known if ext_proc will initiate a local response or not. So the filter
  // will buffer the data while waiting for the response headers.
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  // At this point Envoy knows that local response had started and the request
  // body should be discarded since it does not need to be sent to ext_proc server.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// TODO(yavlasov): needs gRPC message queue
TEST_F(StreamingLocalReplyTest, DISABLED_RequestWithBodyTogetherDuplexAndLocalHeadersOnlyResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");
  // In full duplex mode the filter will send the data to ext_proc server as soon as it is received
  // and tell filter manager not to buffer.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithBodyDuplexAndLocalHeadersOnlyResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// TODO(yavlasov): needs gRPC message queue
TEST_F(StreamingLocalReplyTest, DISABLED_RequestWithBodyTogetherDuplexAndLocalResponseWithBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("foo");
  // In full duplex mode the filter will send the data to ext_proc server as soon as it is received
  // and tell filter manager not to buffer.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody&, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  data.add("bar");
  // This data block is immediately sent to the ext_proc server so the return value is
  // StopIterationNoBuffer.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "bar");
        makeLocalResponseDuplexBody(body_resp, "local response body 2", false);
      });

  data.add("baz");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "baz");
        makeLocalResponseDuplexBody(body_resp, "local response body 3", true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithBodyDuplexAndLocalResponseWithBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  // In full duplex mode the filter will send the data to ext_proc server as soon as it is received
  // and tell filter manager not to buffer.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "foo");
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  data.add("bar");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "bar");
        makeLocalResponseDuplexBody(body_resp, "local response body 3", true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, RequestWithLargeBodyDuplexAndLocalResponseWithBody) {
  // This test differs from one above in that it receives body while waiting for ext_proc
  // server body response.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(data.length(), 0);
  data.add("bar");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(data.length(), 0);

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody&, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        // the second call to decodeData overwrites ext_proc ProcessingRequest in the
        // mocked AsyncGrpcStream.
        // TODO(yavlasov): implement queue of gRPC messages.
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "bar");
        makeLocalResponseDuplexBody(body_resp, "local response body 2", false);
      });

  data.add("baz");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));
  EXPECT_EQ(data.length(), 0);

  EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "baz");
        makeLocalResponseDuplexBody(body_resp, "local response body 3", true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// TODO(yavlasov): needs gRPC message queue
TEST_F(StreamingLocalReplyTest,
       DISABLED_RequestWithBodyAndTrailersTogetherDuplexAndLocalResponseWithBodyAndTrailers) {
  // This test differs from one above in that it receives body while waiting for ext_proc
  // server body response.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "foo");
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  Envoy::Http::TestResponseHeaderMapImpl expected_trailers{{"x-some-other-trailer", "no"}};
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(HeaderMapEqualRef(&expected_trailers)));
  processRequestTrailersAndSendLocalResponse([](const HttpTrailers&, const ProcessingResponse&,
                                                envoy::config::core::v3::HeaderMap& trailers_resp) {
    makeLocalResponseTrailers(trailers_resp);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest,
       RequestWithBodyAndTrailersDuplexAndLocalResponseWithBodyAndTrailers) {
  // This test differs from one above in that it receives body while waiting for ext_proc
  // server body response.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(data.length(), 0);

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "foo");
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  Envoy::Http::TestResponseHeaderMapImpl expected_trailers{{"x-some-other-trailer", "no"}};
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(HeaderMapEqualRef(&expected_trailers)));
  processRequestTrailersAndSendLocalResponse([](const HttpTrailers&, const ProcessingResponse&,
                                                envoy::config::core::v3::HeaderMap& trailers_resp) {
    makeLocalResponseTrailers(trailers_resp);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest,
       RequestWithBodyAndTrailersDuplexAndLocalResponseWithLargerBodyAndTrailers) {
  // This test differs from one above in that it receives body while waiting for ext_proc
  // server body response.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), false));
  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_EQ(data.length(), 0);

  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  processRequestBodyAndSendLocalResponse(
      [](const HttpBody& body, const ProcessingResponse&, StreamedBodyResponse& body_resp) {
        EXPECT_EQ(body.body(), "foo");
        makeLocalResponseDuplexBody(body_resp, "local response body 1", false);
      });

  // Send additional data from the ext_proc server.
  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", false);
  });

  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  Envoy::Http::TestResponseHeaderMapImpl expected_trailers{{"x-some-other-trailer", "no"}};
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(HeaderMapEqualRef(&expected_trailers)));
  processRequestTrailersAndSendLocalResponse([](const HttpTrailers&, const ProcessingResponse&,
                                                envoy::config::core::v3::HeaderMap& trailers_resp) {
    makeLocalResponseTrailers(trailers_resp);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalBodyWithoutHeadersResponse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is configured to fail close on spurious
  // responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "local_response_body_received_before_headers"));
  // Skip sending local response headers and send local response body instead.
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalTrailersWithoutHeadersResponse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // Decoding should not be continued since ext_proc is configured to fail close on spurious
  // responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "local_response_trailers_received_before_headers"));
  // Skip sending local response headers and send local response body instead.
  sendStreamingLocalResponseTrailers(
      [](const ProcessingResponse&, envoy::config::core::v3::HeaderMap& resp) {
        makeLocalResponseTrailers(resp);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalBodyResponseInBufferedMode) {
  expectFailureWithWrongBodyMode(
      "BUFFERED",
      "streaming_local_response_body_is_only_supported_in_NONE_or_FULL_DUPLEX_STREAMED_modes");
}

TEST_F(StreamingLocalReplyTest, LocalBodyResponseInBufferedPartialMode) {
  expectFailureWithWrongBodyMode(
      "BUFFERED_PARTIAL",
      "streaming_local_response_body_is_only_supported_in_NONE_or_FULL_DUPLEX_STREAMED_modes");
}

TEST_F(StreamingLocalReplyTest, LocalBodyResponseInStreamingMode) {
  expectFailureWithWrongBodyMode(
      "STREAMED",
      "streaming_local_response_body_is_only_supported_in_NONE_or_FULL_DUPLEX_STREAMED_modes");
}

TEST_F(StreamingLocalReplyTest, LocalBodyWithoutHeadersResponseWithFailOpen) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  failure_mode_allow: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // In fail open ext_proc will continue request processing if local response has not been started.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Skip sending local response headers and send local response body instead.
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, LocalTrailersWithoutHeadersResponseWithFailOpen) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  failure_mode_allow: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  // In fail open ext_proc will continue request processing if local response has not been started.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Skip sending local response headers and send local response body instead.
  sendStreamingLocalResponseTrailers(
      [](const ProcessingResponse&, envoy::config::core::v3::HeaderMap& resp) {
        makeLocalResponseTrailers(resp);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, InvalidBodyMessageAfterLocalResponseStarted) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  // Decoding should not be continued since ext_proc is configured to fail close on spurious
  // responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false));
  // Start local response streaming.
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "spurious_message"));
  processRequestBody(absl::nullopt, false);
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, InvalidTrailersMessageAfterLocalResponseStarted) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  // Decoding should not be continued since ext_proc is configured to fail close on spurious
  // responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false));
  // Start local response streaming.
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "spurious_message"));

  auto response = std::make_unique<ProcessingResponse>();
  response->mutable_request_trailers();
  stream_callbacks_->onReceiveMessage(std::move(response));

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, InvalidBodyMessageAfterLocalResponseStartedWithFailOpen) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  initialize(R"EOF(
  failure_mode_allow: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  // Decoding should not be continued since ext_proc is configured to fail close on spurious
  // responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  // In local reply mode no data should be injected back into deciding chain.
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false));
  // Start local response streaming.
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, false);
      });

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "spurious_message"));
  auto response = std::make_unique<ProcessingResponse>();
  response->mutable_request_body();
  stream_callbacks_->onReceiveMessage(std::move(response));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, InitiateLocalSteramingResponseWithoutRequestHeaders) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ext_proc_fail_close_spurious_resp", "true"}});
  // Skip sending headers from the client and try to initiate local response streaming
  // when processing request body.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "spurious_message"));

  Buffer::OwnedImpl data("foo");
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, true));

  auto response = std::make_unique<ProcessingResponse>();
  response->mutable_streamed_immediate_response()->mutable_headers_response();
  stream_callbacks_->onReceiveMessage(std::move(response));

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, HttpStatusHeaderIsPreservedEvenWhenPreudoHeadersDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  mutation_rules:
    disallow_system: true
  )EOF");

  // Decoding should not be continued since ext_proc is the terminal filter for local responses.
  EXPECT_CALL(decoder_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Envoy::Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"},
                                                                   {"x-some-other-header", "no"}};
  EXPECT_CALL(decoder_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&expected_response_headers), true));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(StreamingLocalReplyTest, ProcessingStreamingLocalResponse) {
  TestOnProcessingResponseFactory factory;
  Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  on_processing_response:
    name: "abc"
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, false));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        // Indicate to the client that there is body to follow.
        makeLocalResponseHeaders(header_resp, false);
      });
  EXPECT_TRUE(dynamic_metadata_.filter_metadata().contains(
      "envoy-test-ext_proc-streaming_immediate_response"));

  EXPECT_TRUE(dynamic_metadata_.filter_metadata()
                  .at("envoy-test-ext_proc-streaming_immediate_response")
                  .fields()
                  .contains("headers_response"));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, false));
  sendStreamingLocalResponseBody([](const ProcessingResponse&, StreamedBodyResponse& body_resp) {
    makeLocalResponseDuplexBody(body_resp, "local response body", false);
  });

  EXPECT_TRUE(dynamic_metadata_.filter_metadata()
                  .at("envoy-test-ext_proc-streaming_immediate_response")
                  .fields()
                  .contains("body_response"));
  EXPECT_CALL(decoder_callbacks_, encodeTrailers_(_));
  sendStreamingLocalResponseTrailers(
      [](const ProcessingResponse&, envoy::config::core::v3::HeaderMap& trailers_resp) {
        makeLocalResponseTrailers(trailers_resp);
      });

  EXPECT_TRUE(dynamic_metadata_.filter_metadata()
                  .at("envoy-test-ext_proc-streaming_immediate_response")
                  .fields()
                  .contains("trailers_response"));
  filter_->onDestroy();
}

TEST_F(StreamingLocalReplyTest, SaveStreamingLocalResponse) {
  Http::ExternalProcessing::SaveProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  on_processing_response:
    name: "abc"
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.http.ext_proc.response_processors.save_processing_response.v3.SaveProcessingResponse
      save_immediate_response:
        save_response: true
  )EOF");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, true));
  processRequestHeadersAndStartLocalResponse(
      [](const HttpHeaders&, ProcessingResponse&, HttpHeaders& header_resp) {
        makeLocalResponseHeaders(header_resp, true);
      });

  auto filter_state =
      stream_info_.filterState()
          ->getDataMutable<Http::ExternalProcessing::SaveProcessingResponseFilterState>(
              Http::ExternalProcessing::SaveProcessingResponseFilterState::kFilterStateName);
  ASSERT_TRUE(filter_state->response.has_value());

  constexpr absl::string_view expected_proto = R"pb(
    streamed_immediate_response {
      headers_response {
        end_of_stream: true
        headers {
          headers {
            key: ":status"
            raw_value: "200"
          }
          headers {
            key: "x-some-other-header"
            raw_value: "no"
          }
        }
      }
    }
  )pb";
  envoy::service::ext_proc::v3::ProcessingResponse expected_local_response;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(expected_proto, &expected_local_response));
  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_local_response));

  filter_->onDestroy();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
