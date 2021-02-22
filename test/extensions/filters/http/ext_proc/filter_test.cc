#include "extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode;
using envoy::service::ext_proc::v3alpha::ProcessingRequest;
using envoy::service::ext_proc::v3alpha::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::LowerCaseString;

using testing::Eq;
using testing::Invoke;
using testing::Unused;

using namespace std::chrono_literals;

// These tests are all unit tests that directly drive an instance of the
// ext_proc filter and verify the behavior using mocks.

class HttpFilterTest : public testing::Test {
protected:
  void initialize(std::string&& yaml) {
    client_ = std::make_unique<MockClient>();
    EXPECT_CALL(*client_, start(_, _)).WillOnce(Invoke(this, &HttpFilterTest::doStart));

    envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    config_.reset(new FilterConfig(proto_config, 200ms, stats_store_, ""));
    filter_ = std::make_unique<Filter>(config_, std::move(client_));
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks,
                                     const std::chrono::milliseconds& timeout) {
    stream_callbacks_ = &callbacks;
    stream_timeout_ = timeout;

    auto stream = std::make_unique<MockStream>();
    EXPECT_CALL(*stream, send(_, _)).WillRepeatedly(Invoke(this, &HttpFilterTest::doSend));
    EXPECT_CALL(*stream, close()).WillRepeatedly(Invoke(this, &HttpFilterTest::doSendClose));
    return stream;
  }

  void doSend(ProcessingRequest&& request, bool end_stream) {
    ASSERT_FALSE(stream_close_sent_);
    ASSERT_TRUE(last_request_processed_);
    last_request_ = std::move(request);
    last_request_processed_ = false;
    if (end_stream) {
      stream_close_sent_ = true;
    }
  }

  void doSendClose() {
    ASSERT_FALSE(stream_close_sent_);
    stream_close_sent_ = true;
  }

  std::unique_ptr<MockClient> client_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  ProcessingRequest last_request_;
  bool last_request_processed_ = true;
  bool stream_close_sent_ = false;
  std::chrono::milliseconds stream_timeout_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<Filter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Buffer::OwnedImpl data_;
};

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an empty response
TEST_F(HttpFilterTest, SimplestPost) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  )EOF");

  EXPECT_TRUE(config_->failureModeAllow());

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 10);
  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Verify that call was received by mock gRPC server
  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());
  const auto request_headers = last_request_.request_headers();
  EXPECT_FALSE(request_headers.end_of_stream());

  Http::TestRequestHeaderMapImpl expected{{":path", "/"},
                                          {":method", "POST"},
                                          {":scheme", "http"},
                                          {"host", "host"},
                                          {"content-type", "text/plain"},
                                          {"content-length", "10"},
                                          {"x-some-other-header", "yes"}};
  EXPECT_THAT(request_headers.headers(), HeaderProtosEqual(expected));
  last_request_processed_ = true;

  // Send back a response
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->encodeHeaders(response_headers_, false));

  // Expect another stream message
  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_response_headers());
  const auto response_headers = last_request_.response_headers();
  EXPECT_FALSE(response_headers.end_of_stream());

  Http::TestRequestHeaderMapImpl expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_THAT(response_headers.headers(), HeaderProtosEqual(expected_response));
  last_request_processed_ = true;

  // Send back a response
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  std::unique_ptr<ProcessingResponse> resp2 = std::make_unique<ProcessingResponse>();
  resp2->mutable_response_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp2));

  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with a message that modifies the request
// headers.
TEST_F(HttpFilterTest, PostAndChangeHeaders) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");
  request_headers_.addCopy(LowerCaseString("x-do-we-want-this"), "no");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto req_headers_response = resp1->mutable_request_headers();
  auto headers_mut = req_headers_response->mutable_response()->mutable_header_mutation();
  auto add1 = headers_mut->add_set_headers();
  add1->mutable_header()->set_key("x-new-header");
  add1->mutable_header()->set_value("new");
  add1->mutable_append()->set_value(false);
  auto add2 = headers_mut->add_set_headers();
  add2->mutable_header()->set_key("x-some-other-header");
  add2->mutable_header()->set_value("no");
  add2->mutable_append()->set_value(true);
  *headers_mut->add_remove_headers() = "x-do-we-want-this";
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  // We should now have changed the original header a bit
  Http::TestRequestHeaderMapImpl expected{{":path", "/"},
                                          {":method", "POST"},
                                          {":scheme", "http"},
                                          {"host", "host"},
                                          {"x-new-header", "new"},
                                          {"x-some-other-header", "yes"},
                                          {"x-some-other-header", "no"}};
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected));
  last_request_processed_ = true;

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->encodeHeaders(response_headers_, false));

  // Expect another stream message
  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_response_headers());
  const auto response_headers = last_request_.response_headers();
  EXPECT_FALSE(response_headers.end_of_stream());

  Http::TestRequestHeaderMapImpl expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_TRUE(ExtProcTestUtility::headerProtosEqualIgnoreOrder(expected_response,
                                                               response_headers.headers()));
  last_request_processed_ = true;

  // Send back a response
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  std::unique_ptr<ProcessingResponse> resp2 = std::make_unique<ProcessingResponse>();
  auto resp_headers = resp2->mutable_response_headers();
  auto resp_headers_mut = resp_headers->mutable_response()->mutable_header_mutation();
  auto resp_add1 = resp_headers_mut->add_set_headers();
  resp_add1->mutable_header()->set_key("x-new-header");
  resp_add1->mutable_header()->set_value("new");
  stream_callbacks_->onReceiveMessage(std::move(resp2));

  // We should now have changed the original header a bit
  Http::TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                         {"content-type", "text/plain"},
                                                         {"content-length", "3"},
                                                         {"x-new-header", "new"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an "immediate response" message
// that should result in a response being directly sent downstream with
// custom headers.
TEST_F(HttpFilterTest, PostAndRespondImmediately) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got a bad request"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp1->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::BadRequest);
  immediate_response->set_body("Bad request");
  immediate_response->set_details("Got a bad request");
  auto* immediate_headers = immediate_response->mutable_headers();
  auto* hdr1 = immediate_headers->add_set_headers();
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_value("text/plain");
  auto* hdr2 = immediate_headers->add_set_headers();
  hdr2->mutable_append()->set_value(true);
  hdr2->mutable_header()->set_key("x-another-thing");
  hdr2->mutable_header()->set_value("1");
  auto* hdr3 = immediate_headers->add_set_headers();
  hdr3->mutable_append()->set_value(true);
  hdr3->mutable_header()->set_key("x-another-thing");
  hdr3->mutable_header()->set_value("2");
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {"content-type", "text/plain"}, {"x-another-thing", "1"}, {"x-another-thing", "2"}};
  EXPECT_THAT(&immediate_response_headers, HeaderMapEqualIgnoreOrder(&expected_response_headers));

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an "immediate response" message
// during response headers processing that should result in a response being
// directly sent downstream with custom headers.
TEST_F(HttpFilterTest, PostAndRespondImmediatelyOnResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());
  last_request_processed_ = true;

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_response_headers());
  last_request_processed_ = true;

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got a bad request"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp2 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp2->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::BadRequest);
  immediate_response->set_body("Bad request");
  immediate_response->set_details("Got a bad request");
  stream_callbacks_->onReceiveMessage(std::move(resp2));
  EXPECT_TRUE(immediate_response_headers.empty());

  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an empty immediate_response message
TEST_F(HttpFilterTest, RespondImmediatelyDefault) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::OK, "", _, Eq(absl::nullopt), ""))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_immediate_response();
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  EXPECT_TRUE(immediate_response_headers.empty());

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an immediate response message
// that contains a non-default gRPC status.
TEST_F(HttpFilterTest, RespondImmediatelyGrpcError) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::Forbidden, "", _, Eq(999), ""))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp1->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::Forbidden);
  immediate_response->mutable_grpc_status()->set_status(999);
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  EXPECT_TRUE(immediate_response_headers.empty());

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);
}

// Using the default configuration, test the filter with a processor that
// returns an error from from the gRPC stream.
TEST_F(HttpFilterTest, PostAndFail) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_FALSE(config_->failureModeAllow());

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_FALSE(stream_close_sent_);

  // Oh no! The remote server had a failure!
  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc error: gRPC error 13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  // The other side closed the stream
  EXPECT_FALSE(stream_close_sent_);
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
}

// Using the default configuration, test the filter with a processor that
// returns an error from from the gRPC stream during response header processing.
TEST_F(HttpFilterTest, PostAndFailOnResponse) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_FALSE(config_->failureModeAllow());

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());
  last_request_processed_ = true;

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->encodeHeaders(response_headers_, false));

  // Oh no! The remote server had a failure!
  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc error: gRPC error 13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  // The other side closed the stream
  EXPECT_FALSE(stream_close_sent_);
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
}

// Using the default configuration, test the filter with a processor that
// returns an error from the gRPC stream that is ignored because the
// failure_mode_allow parameter is set.
TEST_F(HttpFilterTest, PostAndIgnoreFailure) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  )EOF");

  EXPECT_TRUE(config_->failureModeAllow());

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_FALSE(stream_close_sent_);

  // Oh no! The remote server had a failure which we will ignore
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  // The other side closed the stream
  EXPECT_FALSE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  EXPECT_EQ(1, config_->stats().failure_mode_allowed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message by closing the gRPC stream.
TEST_F(HttpFilterTest, PostAndClose) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_FALSE(config_->failureModeAllow());

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());

  // Close the stream, which should tell the filter to keep on going
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  stream_callbacks_->onGrpcClose();

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  // The other side closed the stream
  EXPECT_FALSE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a processing mode, configure the filter to only send the request_headers
// message.
TEST_F(HttpFilterTest, ProcessingModeRequestHeadersOnly) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SKIP"
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());
  last_request_processed_ = true;

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Http::TestRequestHeaderMapImpl final_expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  Buffer::OwnedImpl second_chunk("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(second_chunk, false));
  Buffer::OwnedImpl empty_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Use the default configuration, but then override the processing mode
// to disable processing of the response headers.
TEST_F(HttpFilterTest, ProcessingModeOverrideResponseHeaders) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());
  last_request_processed_ = true;

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  resp1->mutable_mode_override()->set_response_header_mode(ProcessingMode::SKIP);
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Http::TestRequestHeaderMapImpl final_expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  Buffer::OwnedImpl second_chunk("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(second_chunk, false));
  Buffer::OwnedImpl empty_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a processing mode, configure the filter to only send the response_headers
// message.
TEST_F(HttpFilterTest, ProcessingModeResponseHeadersOnly) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_response_headers());
  last_request_processed_ = true;

  // Send back a response
  EXPECT_CALL(encoder_callbacks_, continueEncoding());
  std::unique_ptr<ProcessingResponse> resp2 = std::make_unique<ProcessingResponse>();
  resp2->mutable_response_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp2));

  Http::TestRequestHeaderMapImpl final_expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  Buffer::OwnedImpl second_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(second_chunk, false));
  Buffer::OwnedImpl empty_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message incorrectly by sending a
// request_body message, which should result in the stream being closed
// and ignored.
TEST_F(HttpFilterTest, OutOfOrder) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_, "POST");
  EXPECT_EQ(FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  EXPECT_FALSE(stream_close_sent_);
  ASSERT_TRUE(last_request_.has_request_headers());

  // Return an out-of-order message. The server should close the stream
  // and continue as if nothing happened.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_body();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  data_.add("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  data_.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  // We closed the stream
  EXPECT_TRUE(stream_close_sent_);

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy