#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/filter_test_common.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using ::envoy::service::ext_proc::v3::BodyResponse;
using ::envoy::service::ext_proc::v3::HeadersResponse;
using ::envoy::service::ext_proc::v3::HttpBody;
using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::ProcessingResponse;

using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterFactoryCb;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::LowerCaseString;
using ::Envoy::Http::RequestHeaderMapPtr;
using ::Envoy::Http::ResponseHeaderMapPtr;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;

using ::testing::Invoke;
using ::testing::Unused;

// Set allow_mode_override in filter config to be true.
// Set request_body_mode: FULL_DUPLEX_STREAMED
// In such case, the mode_override in the response will be ignored.
TEST_F(HttpFilterTest, DisableResponseModeOverrideByStreamedBodyMode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "FULL_DUPLEX_STREAMED"
    response_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
    response_trailer_mode: "SEND"
  allow_mode_override: true
  )EOF");

  EXPECT_EQ(filter_->config().allowModeOverride(), true);
  EXPECT_EQ(filter_->config().sendBodyWithoutWaitingForHeaderResponse(), false);
  EXPECT_EQ(filter_->config().processingMode().response_header_mode(), ProcessingMode::SEND);
  EXPECT_EQ(filter_->config().processingMode().response_body_mode(),
            ProcessingMode::FULL_DUPLEX_STREAMED);
  EXPECT_EQ(filter_->config().processingMode().request_body_mode(),
            ProcessingMode::FULL_DUPLEX_STREAMED);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  // When ext_proc server sends back the request header response, it contains the
  // mode_override for the response_header_mode to be SKIP.
  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse& response, HeadersResponse&) {
        response.mutable_mode_override()->set_response_header_mode(ProcessingMode::SKIP);
      });

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, true));

  // Verify such mode_override is ignored. The response header is still sent to the ext_proc server.
  processResponseHeaders(false, [](const HttpHeaders& header_resp, ProcessingResponse&,
                                   HeadersResponse&) {
    EXPECT_TRUE(header_resp.end_of_stream());
    TestRequestHeaderMapImpl expected_response{{":status", "200"}, {"content-type", "text/plain"}};
    EXPECT_THAT(header_resp.headers(), HeaderProtosEqual(expected_response));
  });

  TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                   {"content-type", "text/plain"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, DuplexStreamedBodyProcessingTestNormal) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  // Test 7x3 streaming.
  for (int i = 0; i < 7; i++) {
    // 7 request chunks are sent to the ext_proc server.
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }

  processResponseBodyHelper(" AAAAA ", want_response_body);
  processResponseBodyHelper(" BBBB ", want_response_body);
  processResponseBodyHelper(" CCC ", want_response_body);

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  // Now do 1:1 streaming for a few chunks.
  for (int i = 0; i < 3; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    processResponseBodyHelper(std::to_string(i), want_response_body);
  }

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  // Now send another 10 chunks.
  for (int i = 0; i < 10; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 10);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }
  // Send the last chunk.
  Buffer::OwnedImpl last_resp_chunk;
  TestUtility::feedBufferWithRandomCharacters(last_resp_chunk, 10);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));

  processResponseBodyHelper(" EEEEEEE ", want_response_body);
  processResponseBodyHelper(" F ", want_response_body);
  processResponseBodyHelper(" GGGGGGGGG ", want_response_body);
  EXPECT_EQ(0, config_->stats().streams_closed_.value());
  processResponseBodyHelper(" HH ", want_response_body, true, true);
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);
  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(HttpFilterTest, DuplexStreamedBodyProcessingTestWithTrailer) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  // Server sending headers response without waiting for body.
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 7; i++) {
    // 7 request chunks are sent to the ext_proc server.
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }

  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));

  processResponseBodyStreamedAfterTrailer(" AAAAA ", want_response_body);
  processResponseBodyStreamedAfterTrailer(" BBBB ", want_response_body);
  EXPECT_EQ(0, config_->stats().streams_closed_.value());
  processResponseTrailers(absl::nullopt, true);
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(HttpFilterTest, DuplexStreamedBodyProcessingTestWithHeaderAndTrailer) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_header_mode: "SEND"
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_TRUE(last_request_.has_protocol_config());
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  // Server buffer header, body and trailer before sending header response.
  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 7; i++) {
    // 7 request chunks are sent to the ext_proc server.
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, false));
  }

  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));

  EXPECT_FALSE(last_request_.has_protocol_config());
  // Server now sends back response.
  processResponseHeadersAfterTrailer(absl::nullopt);
  processResponseBodyStreamedAfterTrailer(" AAAAA ", want_response_body);
  processResponseBodyStreamedAfterTrailer(" BBBB ", want_response_body);
  processResponseTrailers(absl::nullopt, true);

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, DuplexStreamedBodyProcessingTestWithHeaderAndTrailerNoBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_header_mode: "SEND"
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  // Envoy sends header, body and trailer.
  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));

  // Server now sends back response.
  processResponseHeadersAfterTrailer(absl::nullopt);
  processResponseTrailers(absl::nullopt, true);

  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, DuplexStreamedBodyProcessingTestWithFilterConfigMissing) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "STREAMED"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  for (int i = 0; i < 4; i++) {
    // 4 request chunks are sent to the ext_proc server.
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }

  processResponseBody(
      [](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_body("AAA");
      },
      false);

  // Verify spurious message is received.
  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 1);
  filter_->onDestroy();
}

// For FULL_DUPLEX_STREAMED mode, if data is already sent, even fail open
// is configured, Envoy still does fail close.
TEST_F(HttpFilterTest, FullDuplexFailCloseWithDataOutbound) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  processing_mode:
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_chunk;
  TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, true));

  processResponseBody(
      [](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
        auto* body_mut = resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("AAA");
      },
      false);

  // Fail close with spurious messages received.
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(0, config_->stats().failure_mode_allowed_.value());
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, FullDuplexFailOpenWithoutDataOutbound) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  processing_mode:
    response_body_mode: "FULL_DUPLEX_STREAMED"
    response_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  // Fail open with gRPC error messages received.
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");
  EXPECT_EQ(1, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, FullDuplexFailCloseWithDataInbound) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  processing_mode:
    request_body_mode: "FULL_DUPLEX_STREAMED"
    request_trailer_mode: "SEND"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);
  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));

  // Fail close with gRPC error messages received.
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");
  EXPECT_EQ(0, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
  filter_->onDestroy();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
