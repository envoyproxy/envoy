#include <chrono>
#include <cstdint>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/filter_test_common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::envoy::service::ext_proc::v3::HeadersResponse;
using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::ProcessingResponse;

using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::LowerCaseString;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;

using ::testing::Invoke;
using ::testing::Return;
using ::testing::Unused;

TEST_F(HttpFilterTest, HeaderProcessingInObservabilityMode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  observability_mode: true
  )EOF");

  EXPECT_TRUE(config_->observabilityMode());
  observability_mode_ = true;

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 10);
  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");

  // In the observability mode, the filter returns `Continue` in all events of http request
  // lifecycle.
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false,
                        [](const HttpHeaders& header_req, ProcessingResponse&, HeadersResponse&) {
                          EXPECT_FALSE(header_req.end_of_stream());
                          TestRequestHeaderMapImpl expected{{":path", "/"},
                                                            {":method", "POST"},
                                                            {":scheme", "http"},
                                                            {"host", "host"},
                                                            {"content-type", "text/plain"},
                                                            {"content-length", "10"},
                                                            {"x-some-other-header", "yes"}};
                          EXPECT_THAT(header_req.headers(), HeaderProtosEqual(expected));
                        });

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  // In the observability mode, the filter returns `Continue` in all events of http response
  // lifecycle.
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(
      false, [](const HttpHeaders& header_resp, ProcessingResponse&, HeadersResponse&) {
        EXPECT_FALSE(header_resp.end_of_stream());
        TestRequestHeaderMapImpl expected_response{
            {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
        EXPECT_THAT(header_resp.headers(), HeaderProtosEqual(expected_response));
      });

  Buffer::OwnedImpl resp_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  deferred_close_timer_ = new Event::MockTimer(&dispatcher_);
  // Deferred close timer is expected to be enabled by `DeferredDeletableStream`'s deferredClose(),
  // which is triggered by filter onDestroy() function below.
  EXPECT_CALL(*deferred_close_timer_,
              enableTimer(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS), _));
  filter_->onDestroy();
  deferred_close_timer_->invokeCallback();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  // Two messages (request and response header message) are sent.
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  // No response is received in observability mode.
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  // Deferred stream is closed.
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(HttpFilterTest, StreamingBodiesInObservabilityMode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  observability_mode: true
  processing_mode:
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
  )EOF");

  uint32_t content_length = 100;
  observability_mode_ = true;

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), content_length);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);
  // In observability mode, content length is not removed as there is no mutation from ext_proc
  // server.
  EXPECT_EQ(request_headers_.getContentLengthValue(), absl::StrCat(content_length));

  Buffer::OwnedImpl want_request_body;
  Buffer::OwnedImpl got_request_body;
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, true))
      .WillRepeatedly(Invoke(
          [&got_request_body](Buffer::Instance& data, Unused) { got_request_body.move(data); }));

  Buffer::OwnedImpl req_chunk_1;
  TestUtility::feedBufferWithRandomCharacters(req_chunk_1, 100);
  want_request_body.add(req_chunk_1.toString());
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_chunk_1, true));
  got_request_body.move(req_chunk_1);
  EXPECT_EQ(want_request_body.toString(), got_request_body.toString());

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), content_length);

  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);
  EXPECT_EQ(response_headers_.getContentLengthValue(), absl::StrCat(content_length));

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    got_response_body.move(resp_chunk);
  }

  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(last_resp_chunk, true));

  // At this point, since we injected the data from each chunk after the "encodeData"
  // callback, and since we also injected any chunks inserted using "injectEncodedData,"
  // the two buffers should match!
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());

  deferred_close_timer_ = new Event::MockTimer(&dispatcher_);
  // Deferred close timer is expected to be enabled by `DeferredDeletableStream`'s deferredClose(),
  // which is triggered by filter onDestroy() function.
  EXPECT_CALL(*deferred_close_timer_,
              enableTimer(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS), _));
  filter_->onDestroy();
  deferred_close_timer_->invokeCallback();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(9, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(HttpFilterTest, StreamingAllDataInObservabilityMode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  observability_mode: true
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SEND"
    response_trailer_mode: "SEND"
  )EOF");

  observability_mode_ = true;

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  const uint32_t chunk_number = 20;
  sendChunkRequestData(chunk_number, true);
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  processRequestTrailers(absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);
  sendChunkResponseData(chunk_number * 2, true);
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  processResponseTrailers(absl::nullopt, false);
  deferred_close_timer_ = new Event::MockTimer(&dispatcher_);
  // Deferred close timer is expected to be enabled by `DeferredDeletableStream`'s deferredClose(),
  // which is triggered by filter onDestroy() function.
  EXPECT_CALL(*deferred_close_timer_,
              enableTimer(std::chrono::milliseconds(DEFAULT_DEFERRED_CLOSE_TIMEOUT_MS), _));
  filter_->onDestroy();
  deferred_close_timer_->invokeCallback();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  // Total gRPC messages include two headers and two trailers on top of the req/resp chunk data.
  uint32_t total_msg = 3 * chunk_number + 4;
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
