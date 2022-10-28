#include <algorithm>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stream_info/mocks.h"
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

using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeadersResponse;
using envoy::service::ext_proc::v3::HttpBody;
using envoy::service::ext_proc::v3::HttpHeaders;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::LowerCaseString;

using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::ReturnRef;
using testing::Unused;

using namespace std::chrono_literals;

static const uint32_t BufferSize = 100000;

// These tests are all unit tests that directly drive an instance of the
// ext_proc filter and verify the behavior using mocks.

class HttpFilterTest : public testing::Test {
protected:
  void initialize(std::string&& yaml) {
    client_ = std::make_unique<MockClient>();
    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(*client_, start(_, _, _)).WillOnce(Invoke(this, &HttpFilterTest::doStart));
    EXPECT_CALL(encoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(route_));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](Unused) {
          // Create a mock timer that we can check at destruction time to see if
          // all timers were disabled no matter what. MockTimer has default
          // actions that we just have to enable properly here.
          auto* timer = new Event::MockTimer();
          EXPECT_CALL(*timer, enableTimer(_, _)).Times(AnyNumber());
          EXPECT_CALL(*timer, disableTimer()).Times(AnyNumber());
          EXPECT_CALL(*timer, enabled()).Times(AnyNumber());
          timers_.push_back(timer);
          return timer;
        }));

    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    config_.reset(new FilterConfig(proto_config, 200ms, stats_store_, ""));
    filter_ = std::make_unique<Filter>(config_, std::move(client_), grpc_service_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(Return(BufferSize));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(BufferSize));
    HttpTestUtility::addDefaultHeaders(request_headers_);
    request_headers_.setMethod("POST");
  }

  void TearDown() override {
    // This will fail if, at the end of the test, we left any timers enabled.
    // (This particular test suite does not actually let timers expire,
    // although other test suites do.)
    EXPECT_TRUE(allTimersDisabled());
  }

  bool allTimersDisabled() {
    for (auto* t : timers_) {
      if (t->enabled_) {
        return false;
      }
    }
    return true;
  }

  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks, testing::Unused,
                                     testing::Unused) {
    stream_callbacks_ = &callbacks;

    auto stream = std::make_unique<MockStream>();
    // We never send with the "close" flag set
    EXPECT_CALL(*stream, send(_, false)).WillRepeatedly(Invoke(this, &HttpFilterTest::doSend));
    // close is idempotent and only called once per filter
    EXPECT_CALL(*stream, close()).WillOnce(Invoke(this, &HttpFilterTest::doSendClose));
    return stream;
  }

  void doSend(ProcessingRequest&& request, Unused) { last_request_ = std::move(request); }

  bool doSendClose() { return !server_closed_stream_; }

  void setUpDecodingBuffering(Buffer::Instance& buf, bool expect_modification = false) {
    EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
        .WillRepeatedly(
            Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
    if (expect_modification) {
      EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
          .WillOnce(
              Invoke([&buf](std::function<void(Buffer::Instance&)> callback) { callback(buf); }));
    }
  }

  void setUpEncodingBuffering(Buffer::Instance& buf, bool expect_modification = false) {
    EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&buf));
    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
        .WillRepeatedly(
            Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
    if (expect_modification) {
      EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
          .WillOnce(
              Invoke([&buf](std::function<void(Buffer::Instance&)> callback) { callback(buf); }));
    }
  }

  void setUpDecodingWatermarking(bool& watermarked) {
    EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark())
        .WillRepeatedly(Invoke([&watermarked]() {
          EXPECT_FALSE(watermarked);
          watermarked = true;
        }));
    EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark())
        .WillRepeatedly(Invoke([&watermarked]() {
          EXPECT_TRUE(watermarked);
          watermarked = false;
        }));
  }

  void setUpEncodingWatermarking(bool& watermarked) {
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark())
        .WillRepeatedly(Invoke([&watermarked]() {
          EXPECT_FALSE(watermarked);
          watermarked = true;
        }));
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark())
        .WillRepeatedly(Invoke([&watermarked]() {
          EXPECT_TRUE(watermarked);
          watermarked = false;
        }));
  }

  // Expect a request_headers request, and send back a valid response.
  void processRequestHeaders(
      bool buffering_data,
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
          cb) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_request_headers());
    const auto& headers = last_request_.request_headers();
    auto response = std::make_unique<ProcessingResponse>();
    auto* headers_response = response->mutable_request_headers();
    if (cb) {
      (*cb)(headers, *response, *headers_response);
    }
    if (!buffering_data) {
      EXPECT_CALL(decoder_callbacks_, continueDecoding());
    }
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // Expect a response_headers request, and send back a valid response
  void processResponseHeaders(
      bool buffering_data,
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
          cb) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_response_headers());
    const auto& headers = last_request_.response_headers();
    auto response = std::make_unique<ProcessingResponse>();
    auto* headers_response = response->mutable_response_headers();
    if (cb) {
      (*cb)(headers, *response, *headers_response);
    }
    if (!buffering_data) {
      EXPECT_CALL(encoder_callbacks_, continueEncoding());
    }
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // Expect a request_body request, and send back a valid response
  void processRequestBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_request_body());
    const auto& body = last_request_.request_body();
    auto response = std::make_unique<ProcessingResponse>();
    auto* body_response = response->mutable_request_body();
    if (cb) {
      (*cb)(body, *response, *body_response);
    }
    if (should_continue) {
      EXPECT_CALL(decoder_callbacks_, continueDecoding());
    }
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // Expect a request_body request, and send back a valid response
  void processResponseBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_response_body());
    const auto& body = last_request_.response_body();
    auto response = std::make_unique<ProcessingResponse>();
    auto* body_response = response->mutable_response_body();
    if (cb) {
      (*cb)(body, *response, *body_response);
    }
    if (should_continue) {
      EXPECT_CALL(encoder_callbacks_, continueEncoding());
    }
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // The number of processor grpc calls made in the encoding and decoding path.
  void expectGrpcCalls(const envoy::config::core::v3::TrafficDirection traffic_direction,
                       const Grpc::Status::GrpcStatus status, const int expected_calls_count) {
    const ExtProcLoggingInfo::GrpcCalls& grpc_calls =
        stream_info_.filterState()
            ->getDataReadOnly<
                Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
                Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfoName)
            ->grpcCalls(traffic_direction);
    int calls_count = std::count_if(
        grpc_calls.begin(), grpc_calls.end(),
        [&](ExtProcLoggingInfo::GrpcCall grpc_call) { return grpc_call.status_ == status; });
    EXPECT_EQ(calls_count, expected_calls_count);
  }

  envoy::config::core::v3::GrpcService grpc_service_;
  std::unique_ptr<MockClient> client_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  ProcessingRequest last_request_;
  bool server_closed_stream_ = false;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<Filter> filter_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Router::RouteConstSharedPtr route_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  std::vector<Event::MockTimer*> timers_;
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
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 10);
  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(false,
                        [](const HttpHeaders& header_req, ProcessingResponse&, HeadersResponse&) {
                          EXPECT_FALSE(header_req.end_of_stream());
                          Http::TestRequestHeaderMapImpl expected{{":path", "/"},
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(
      false, [](const HttpHeaders& header_resp, ProcessingResponse&, HeadersResponse&) {
        EXPECT_FALSE(header_resp.end_of_stream());
        Http::TestRequestHeaderMapImpl expected_response{
            {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
        EXPECT_THAT(header_resp.headers(), HeaderProtosEqual(expected_response));
      });

  Buffer::OwnedImpl resp_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND, Grpc::Status::Ok, 1);
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::OUTBOUND, Grpc::Status::Ok, 1);
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

  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");
  request_headers_.addCopy(LowerCaseString("x-do-we-want-this"), "no");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& header_resp) {
        auto headers_mut = header_resp.mutable_response()->mutable_header_mutation();
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_header()->set_key("x-new-header");
        add1->mutable_header()->set_value("new");
        add1->mutable_append()->set_value(false);
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header");
        add2->mutable_header()->set_value("no");
        add2->mutable_append()->set_value(true);
        *headers_mut->add_remove_headers() = "x-do-we-want-this";
      });

  // We should now have changed the original header a bit
  Http::TestRequestHeaderMapImpl expected{{":path", "/"},
                                          {":method", "POST"},
                                          {":scheme", "http"},
                                          {"host", "host"},
                                          {"x-new-header", "new"},
                                          {"x-some-other-header", "yes"},
                                          {"x-some-other-header", "no"}};
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected));

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders& response_headers, ProcessingResponse&,
                                   HeadersResponse& header_resp) {
    EXPECT_FALSE(response_headers.end_of_stream());
    Http::TestRequestHeaderMapImpl expected_response{
        {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
    EXPECT_THAT(response_headers.headers(), HeaderProtosEqual(expected_response));

    auto* resp_headers_mut = header_resp.mutable_response()->mutable_header_mutation();
    auto* resp_add1 = resp_headers_mut->add_set_headers();
    resp_add1->mutable_header()->set_key("x-new-header");
    resp_add1->mutable_header()->set_value("new");
  });

  // We should now have changed the original header a bit
  Http::TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                         {"content-type", "text/plain"},
                                                         {"content-length", "3"},
                                                         {"x-new-header", "new"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got_a_bad_request"))
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

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND, Grpc::Status::Ok, 1);
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::OUTBOUND, Grpc::Status::Ok, 0);
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(false, absl::nullopt);

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_response_headers());

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got_a_bad_request"))
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

  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with buffering set for the request body,
// test the filter with a processor that changes the request body,
// passing the data in a single chunk.
TEST_F(HttpFilterTest, PostAndChangeRequestBodyBuffered) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "BUFFERED"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(true, absl::nullopt);

  Buffer::OwnedImpl req_data;
  TestUtility::feedBufferWithRandomCharacters(req_data, 100);
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data, true);

  // Testing the case where we just have one chunk of data and it is buffered.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_data, true));
  processRequestBody(
      [&buffered_data](const HttpBody& req_body, ProcessingResponse&, BodyResponse& body_resp) {
        EXPECT_TRUE(req_body.end_of_stream());
        EXPECT_EQ(100, req_body.body().size());
        EXPECT_EQ(req_body.body(), buffered_data.toString());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Replaced!");
      });
  // Expect that the original buffer is replaced.
  EXPECT_EQ("Replaced!", buffered_data.toString());
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with buffering set for the request body,
// test the filter with a processor that changes the request body,
// passing the data in chunks that come before the request callback
// is complete.
TEST_F(HttpFilterTest, PostAndChangeRequestBodyBufferedComesFast) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "BUFFERED"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data_1("Hello");
  Buffer::OwnedImpl req_data_2(", ");
  Buffer::OwnedImpl req_data_3("there, ");
  Buffer::OwnedImpl req_data_4("World!");
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data);

  // Buffering and callback isn't complete so we should watermark
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);

  // Pretend that Envoy ignores the watermark and keep sending. It often does!
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_2, false));
  buffered_data.add(req_data_2);
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_3, false));
  buffered_data.add(req_data_3);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_4, true));
  buffered_data.add(req_data_4);

  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  processRequestHeaders(true, absl::nullopt);

  processRequestBody([](const HttpBody& req_body, ProcessingResponse&, BodyResponse&) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ("Hello, there, World!", req_body.body());
  });
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
}

// Using a configuration with buffering set for the request body,
// test the filter with a processor that changes the request body,
// passing the data so that some chunks come before the request callback
// is complete and others come later.
TEST_F(HttpFilterTest, PostAndChangeRequestBodyBufferedComesALittleFast) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "BUFFERED"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data_1("Hello");
  Buffer::OwnedImpl req_data_2(", ");
  Buffer::OwnedImpl req_data_3("there, ");
  Buffer::OwnedImpl req_data_4("World!");
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data);

  // Buffering and callback isn't complete so we should watermark
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_2, false));
  buffered_data.add(req_data_2);

  // Now the headers response comes in before we get all the data
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  processRequestHeaders(true, absl::nullopt);

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_3, false));
  buffered_data.add(req_data_3);
  // In this case, the filter builds the buffer on the last call
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_data_4, true));

  processRequestBody([](const HttpBody& req_body, ProcessingResponse&, BodyResponse&) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ("Hello, there, World!", req_body.body());
  });
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
}

// Using a configuration with buffering set for the request and
// response bodies but not the headers, and with each body
// delivered as a single chunk, test the filter with a processor that
// clears the request body and changes the response body.
TEST_F(HttpFilterTest, PostAndChangeBothBodiesBufferedOneChunk) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_body_mode: "BUFFERED"
    response_body_mode: "BUFFERED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data;
  TestUtility::feedBufferWithRandomCharacters(req_data, 100);
  Buffer::OwnedImpl buffered_request_data;
  setUpDecodingBuffering(buffered_request_data, true);

  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_data, true));

  processRequestBody([&buffered_request_data](const HttpBody& req_body, ProcessingResponse&,
                                              BodyResponse& body_resp) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ(buffered_request_data.toString(), req_body.body());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_clear_body(true);
  });
  EXPECT_EQ(0, buffered_request_data.length());

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "100");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl resp_data;
  TestUtility::feedBufferWithRandomCharacters(resp_data, 100);
  Buffer::OwnedImpl buffered_response_data;
  setUpEncodingBuffering(buffered_response_data, true);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));

  processResponseBody([&buffered_response_data](const HttpBody& req_body, ProcessingResponse&,
                                                BodyResponse& body_resp) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ(buffered_response_data.toString(), req_body.body());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_body("Hello, World!");
  });
  EXPECT_EQ("Hello, World!", buffered_response_data.toString());

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with buffering set for the request and
// response bodies but not the headers, and with each body
// delivered as set of chunks, test the filter with a processor that
// clears the request body and changes the response body.
TEST_F(HttpFilterTest, PostAndChangeBothBodiesBufferedMultiChunk) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_body_mode: "BUFFERED"
    response_body_mode: "BUFFERED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data;
  TestUtility::feedBufferWithRandomCharacters(req_data, 100);
  Buffer::OwnedImpl buffered_req_data;
  Buffer::OwnedImpl empty_data;
  setUpDecodingBuffering(buffered_req_data, true);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data, false));
  // At this point, Envoy adds data to the buffer
  buffered_req_data.add(req_data);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_data, true));

  processRequestBody(
      [&buffered_req_data](const HttpBody& req_body, ProcessingResponse&, BodyResponse& body_resp) {
        EXPECT_TRUE(req_body.end_of_stream());
        EXPECT_EQ(buffered_req_data.toString(), req_body.body());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Changed it!");
      });
  // Expect that the buffered data was changed
  EXPECT_EQ("Changed it!", buffered_req_data.toString());

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl resp_data_1;
  TestUtility::feedBufferWithRandomCharacters(resp_data_1, 100);
  Buffer::OwnedImpl resp_data_2;
  TestUtility::feedBufferWithRandomCharacters(resp_data_2, 100);
  Buffer::OwnedImpl resp_data_3;
  TestUtility::feedBufferWithRandomCharacters(resp_data_3, 100);
  Buffer::OwnedImpl buffered_resp_data;
  setUpEncodingBuffering(buffered_resp_data, true);

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_data_1, false));
  // Emulate what Envoy does with this data
  buffered_resp_data.add(resp_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(resp_data_2, false));
  buffered_resp_data.add(resp_data_2);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data_3, true));
  // After this call, the callback should have been used to add the third chunk to the buffer

  processResponseBody([&buffered_resp_data](const HttpBody& req_body, ProcessingResponse&,
                                            BodyResponse& body_resp) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ(buffered_resp_data.toString(), req_body.body());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_clear_body(true);
  });
  EXPECT_EQ(0, buffered_resp_data.length());

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with partial buffering set for the request and
// response bodies but not the headers, and with each body
// delivered as set of chunks, test the filter with a processor that
// clears the request body and changes the response body. This works just
// like buffering because the bodies are much smaller than the buffer limit.
TEST_F(HttpFilterTest, PostAndChangeBothBodiesBufferedPartialMultiChunk) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_body_mode: "BUFFERED_PARTIAL"
    response_body_mode: "BUFFERED_PARTIAL"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl upstream_request_body;
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  Buffer::OwnedImpl req_data;
  TestUtility::feedBufferWithRandomCharacters(req_data, 100);
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  upstream_request_body.move(req_data);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_data, true));
  upstream_request_body.move(empty_data);

  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke([&upstream_request_body](Buffer::Instance& data, Unused) {
        upstream_request_body.move(data);
      }));

  processRequestBody([](const HttpBody& req_body, ProcessingResponse&, BodyResponse& body_resp) {
    EXPECT_TRUE(req_body.end_of_stream());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_body("Changed it!");
  });

  // Expect that all those bodies ended up being the same as the thing we
  // replaced it with.
  EXPECT_EQ("Changed it!", upstream_request_body.toString());

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl expected_response_body;
  Buffer::OwnedImpl resp_data_1;
  TestUtility::feedBufferWithRandomCharacters(resp_data_1, 100);
  expected_response_body.add(resp_data_1.toString());
  Buffer::OwnedImpl resp_data_2;
  TestUtility::feedBufferWithRandomCharacters(resp_data_2, 100);
  expected_response_body.add(resp_data_2.toString());
  Buffer::OwnedImpl resp_data_3;
  TestUtility::feedBufferWithRandomCharacters(resp_data_3, 100);
  expected_response_body.add(resp_data_3.toString());

  Buffer::OwnedImpl downstream_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke([&downstream_response_body](Buffer::Instance& data, Unused) {
        downstream_response_body.move(data);
      }));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_1, false));
  downstream_response_body.move(resp_data_1);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_2, false));
  downstream_response_body.move(resp_data_2);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data_3, true));
  downstream_response_body.move(resp_data_3);

  processResponseBody(
      [&expected_response_body](const HttpBody& req_body, ProcessingResponse&, BodyResponse&) {
        EXPECT_TRUE(req_body.end_of_stream());
        EXPECT_EQ(expected_response_body.toString(), req_body.body());
      });
  // At this point, the whole thing should have been injected to the downstream
  EXPECT_EQ(expected_response_body.toString(), downstream_response_body.toString());

  filter_->onDestroy();
  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with partial buffering set for the request body,
// test the filter when all the data comes in before the headers callback
// response comes back.
TEST_F(HttpFilterTest, PostFastRequestPartialBuffering) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "BUFFERED_PARTIAL"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data_1("Hello");
  Buffer::OwnedImpl req_data_2(", World!");
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data);

  // Buffering and callback isn't complete so we should watermark
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_2, true));
  buffered_data.add(req_data_2);

  // Now the headers response comes in and we are all done
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  processRequestHeaders(true, absl::nullopt);

  processRequestBody([](const HttpBody& req_body, ProcessingResponse&, BodyResponse&) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ("Hello, World!", req_body.body());
  });

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "2");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add("ok");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  filter_->onDestroy();
}

// Using a configuration with partial buffering set for the request body,
// test the filter when the data that comes in before the headers callback
// completes has exceeded the buffer limit.
TEST_F(HttpFilterTest, PostFastAndBigRequestPartialBuffering) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "BUFFERED_PARTIAL"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 13000);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl req_data_1;
  TestUtility::feedBufferWithRandomCharacters(req_data_1, 5000);
  Buffer::OwnedImpl req_data_2;
  TestUtility::feedBufferWithRandomCharacters(req_data_2, 6000);
  Buffer::OwnedImpl req_data_3;
  TestUtility::feedBufferWithRandomCharacters(req_data_3, 2000);
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data, true);
  Buffer::OwnedImpl expected_request_data;
  expected_request_data.add(req_data_1);
  expected_request_data.add(req_data_2);

  // Buffering and callback isn't complete so we should watermark
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_2, false));
  buffered_data.add(req_data_2);

  // Now the headers response comes in. Since we are over the watermark we
  // should send the callback.
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(10000));
  processRequestHeaders(true, absl::nullopt);
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, false));
  processRequestBody(
      [&expected_request_data](const HttpBody& req_body, ProcessingResponse&, BodyResponse&) {
        EXPECT_FALSE(req_body.end_of_stream());
        EXPECT_EQ(expected_request_data.toString(), req_body.body());
      });

  // The rest of the data should continue as normal.
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data_3, true));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "2");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add("ok");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  filter_->onDestroy();
}

// Using a configuration with streaming set for the request and
// response bodies, ensure that the chunks are delivered to the processor and
// that the processor gets them correctly.
TEST_F(HttpFilterTest, PostStreamingBodies) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  bool decoding_watermarked = false;
  setUpDecodingWatermarking(decoding_watermarked);

  Buffer::OwnedImpl want_request_body;
  Buffer::OwnedImpl got_request_body;
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_request_body](Buffer::Instance& data, Unused) { got_request_body.move(data); }));

  Buffer::OwnedImpl req_chunk_1;
  TestUtility::feedBufferWithRandomCharacters(req_chunk_1, 100);
  want_request_body.add(req_chunk_1.toString());
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_chunk_1, true));
  got_request_body.move(req_chunk_1);
  processRequestBody(absl::nullopt);
  EXPECT_EQ(want_request_body.toString(), got_request_body.toString());
  EXPECT_FALSE(decoding_watermarked);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "100");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    got_response_body.move(resp_chunk);
    processResponseBody(absl::nullopt, false);
  }

  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);

  // At this point, since we injected the data from each chunk after the "encodeData"
  // callback, and since we also injected any chunks inserted using "injectEncodedData,"
  // the two buffers should match!
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(9, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(9, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND, Grpc::Status::Ok, 2);
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::OUTBOUND, Grpc::Status::Ok, 7);
}

// Using a configuration with streaming set for the request and
// response bodies, ensure that the chunks are delivered to the processor and
// that the processor gets them correctly when some data comes in before the
// headers are done processing.
TEST_F(HttpFilterTest, PostStreamingBodiesDifferentOrder) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  bool decoding_watermarked = false;
  setUpDecodingWatermarking(decoding_watermarked);

  Buffer::OwnedImpl want_request_body;
  Buffer::OwnedImpl got_request_body;
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_request_body](Buffer::Instance& data, Unused) { got_request_body.move(data); }));

  Buffer::OwnedImpl req_chunk_1;
  TestUtility::feedBufferWithRandomCharacters(req_chunk_1, 100);
  want_request_body.add(req_chunk_1.toString());
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_chunk_1, true));
  got_request_body.move(req_chunk_1);
  processRequestBody(absl::nullopt);
  EXPECT_EQ(want_request_body.toString(), got_request_body.toString());
  EXPECT_FALSE(decoding_watermarked);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "100");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));
  Buffer::OwnedImpl response_buffer;
  setUpEncodingBuffering(response_buffer, true);

  for (int i = 0; i < 3; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->encodeData(resp_chunk, false));
    response_buffer.move(resp_chunk);
  }

  processResponseHeaders(false, absl::nullopt);
  EXPECT_EQ(0, response_buffer.length());
  EXPECT_FALSE(encoding_watermarked);
  got_response_body.move(response_buffer);

  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    got_response_body.move(resp_chunk);
  }

  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));

  // Act as if the callbacks were delayed and send back all the responses now.
  for (int i = 0; i < 7; i++) {
    auto response = std::make_unique<ProcessingResponse>();
    response->mutable_response_body();
    if (i == 6) {
      EXPECT_CALL(encoder_callbacks_, continueEncoding());
    }
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(10, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(10, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with streaming set for the response body,
// change the processing mode after receiving some chunks and verify the
// correct behavior.
TEST_F(HttpFilterTest, GetStreamingBodyAndChangeMode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  // Send three bodies
  for (int i = 0; i < 3; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    got_response_body.move(resp_chunk);
  }

  // Respond to the first one by asking to change the processing mode
  processResponseBody(
      [](const HttpBody&, ProcessingResponse& response, BodyResponse&) {
        response.mutable_mode_override()->set_response_body_mode(ProcessingMode::NONE);
      },
      false);

  // A new body chunk should not be sent to the server, but should be queued
  // because we didn't get all the responses yet
  Buffer::OwnedImpl resp_chunk;
  TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
  want_response_body.add(resp_chunk.toString());
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  got_response_body.move(resp_chunk);

  // There should be two more messages outstanding, but not three, so respond
  // just to them.
  for (int i = 0; i < 2; i++) {
    processResponseBody(absl::nullopt, false);
  }

  // Close the stream
  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, false);

  // At this point, the whole body should have been processed including things
  // that were rejected.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(5, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(5, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with streaming set for the response body,
// change the processing mode after receiving some chunks and verify the
// correct behavior.
TEST_F(HttpFilterTest, GetStreamingBodyAndChangeModeDifferentOrder) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, false))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  // Send three bodies
  for (int i = 0; i < 3; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    want_response_body.add(resp_chunk.toString());
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
    got_response_body.move(resp_chunk);
  }

  // Respond to the first one by asking to change the processing mode
  processResponseBody(
      [](const HttpBody&, ProcessingResponse& response, BodyResponse&) {
        response.mutable_mode_override()->set_response_body_mode(ProcessingMode::NONE);
      },
      false);

  // A new body chunk should not be sent to the server, but should be queued
  // because we didn't get all the responses yet
  Buffer::OwnedImpl resp_chunk;
  TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
  want_response_body.add(resp_chunk.toString());
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, true));
  got_response_body.move(resp_chunk);

  // There should be two more messages outstanding, but not three, so respond
  // just to them.
  processResponseBody(absl::nullopt, false);
  processResponseBody(absl::nullopt, true);

  // At this point, the whole body should have been processed including things
  // that were rejected.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(5, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(5, config_->stats().stream_msgs_received_.value());
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

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

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

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

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
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
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Oh no! The remote server had a failure!
  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND, Grpc::Status::Internal, 1);
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
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  // Oh no! The remote server had a failure!
  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  // The other side closed the stream
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND, Grpc::Status::Ok, 1);
  expectGrpcCalls(envoy::config::core::v3::TrafficDirection::OUTBOUND, Grpc::Status::Internal, 1);
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
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Oh no! The remote server had a failure which we will ignore
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

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
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

  // Close the stream, which should tell the filter to keep on going
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcClose();

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Mimic a downstream client reset while the filter waits for a response from
// the processor.
TEST_F(HttpFilterTest, PostAndDownstreamReset) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_FALSE(config_->failureModeAllow());

  // Create synthetic HTTP request
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_request_headers());
  EXPECT_FALSE(allTimersDisabled());

  // Call onDestroy to mimic a downstream client reset.
  filter_->onDestroy();

  EXPECT_TRUE(allTimersDisabled());
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse& response, HeadersResponse&) {
        response.mutable_mode_override()->set_response_header_mode(ProcessingMode::SKIP);
      });

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

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, absl::nullopt);

  Http::TestRequestHeaderMapImpl final_expected_response{
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  Buffer::OwnedImpl second_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(second_chunk, false));
  Buffer::OwnedImpl empty_chunk;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, verify that the "clear_route_cache_ flag makes the appropriate
// callback on the filter.
TEST_F(HttpFilterTest, ClearRouteCache) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  Buffer::OwnedImpl resp_data("foo");
  Buffer::OwnedImpl buffered_response_data;
  setUpEncodingBuffering(buffered_response_data);

  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));

  EXPECT_CALL(encoder_callbacks_.downstream_callbacks_, clearRouteCache());
  processResponseBody([](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(3, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using the default configuration, turn a GET into a POST.
TEST_F(HttpFilterTest, ReplaceRequest) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  request_headers_.setMethod("GET");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));

  Buffer::OwnedImpl req_buffer;
  setUpDecodingBuffering(req_buffer, true);
  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& hdrs_resp) {
        hdrs_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        auto* hdr = hdrs_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key(":method");
        hdr->mutable_header()->set_value("POST");
        hdrs_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, World!");
      });

  Http::TestRequestHeaderMapImpl expected_request{
      {":scheme", "http"}, {":authority", "host"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected_request));
  EXPECT_EQ(req_buffer.toString(), "Hello, World!");

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data_1;
  TestUtility::feedBufferWithRandomCharacters(resp_data_1, 100);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_1, true));

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// Using a configuration with response mode set up for buffering, replace the complete response.
// This should result in none of the actual response coming back and no callbacks being
// fired.
TEST_F(HttpFilterTest, ReplaceCompleteResponseBuffered) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl resp_data_1;
  TestUtility::feedBufferWithRandomCharacters(resp_data_1, 100);
  Buffer::OwnedImpl resp_data_2;
  TestUtility::feedBufferWithRandomCharacters(resp_data_2, 100);
  Buffer::OwnedImpl buffered_resp_data;
  setUpEncodingBuffering(buffered_resp_data, true);

  processResponseHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& hdrs_resp) {
        hdrs_resp.mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
        auto* hdr = hdrs_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key("x-test-header");
        hdr->mutable_header()->set_value("true");
        hdrs_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, World!");
      });

  // Ensure buffered data was updated
  EXPECT_EQ(buffered_resp_data.toString(), "Hello, World!");

  // Since we did CONTINUE_AND_REPLACE, later data is cleared
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_1, false));
  EXPECT_EQ(resp_data_1.length(), 0);
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_2, true));
  EXPECT_EQ(resp_data_2.length(), 0);

  // No additional messages should come in since we replaced, although they
  // are configured.
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
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

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.async_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

  // Return an out-of-order message. The server should close the stream
  // and continue as if nothing happened.
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_body();
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// When merging two configurations, ensure that the second processing mode
// overrides the first.
TEST(OverrideTest, OverrideProcessingMode) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::STREAMED);
  cfg2.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
      ProcessingMode::BUFFERED);
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  EXPECT_FALSE(route1.disabled());
  EXPECT_EQ(route1.processingMode()->request_header_mode(), ProcessingMode::DEFAULT);
  EXPECT_EQ(route1.processingMode()->request_body_mode(), ProcessingMode::STREAMED);
  EXPECT_EQ(route1.processingMode()->response_body_mode(), ProcessingMode::BUFFERED);
}

// When merging two configurations, if the first processing mode is set, and
// the second is disabled, then the filter should be disabled.
TEST(OverrideTest, DisableOverridesFirstMode) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  ExtProcPerRoute cfg2;
  cfg2.set_disabled(true);
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  EXPECT_TRUE(route1.disabled());
  EXPECT_FALSE(route1.processingMode());
}

// When merging two configurations, if the first override is disabled, and
// the second has a new mode, then the filter should use the new mode.
TEST(OverrideTest, ModeOverridesFirstDisable) {
  ExtProcPerRoute cfg1;
  cfg1.set_disabled(true);
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  EXPECT_FALSE(route1.disabled());
  EXPECT_EQ(route1.processingMode()->request_header_mode(), ProcessingMode::SKIP);
}

// When merging two configurations, if both are disabled, then it's still
// disabled.
TEST(OverrideTest, DisabledThingsAreDisabled) {
  ExtProcPerRoute cfg1;
  cfg1.set_disabled(true);
  ExtProcPerRoute cfg2;
  cfg2.set_disabled(true);
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  EXPECT_TRUE(route1.disabled());
  EXPECT_FALSE(route1.processingMode());
}

// When merging two configurations, second grpc_service overrides the first.
TEST(OverrideTest, GrpcServiceOverride) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_2");
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  ASSERT_TRUE(route1.grpcService().has_value());
  EXPECT_THAT(*route1.grpcService(), ProtoEq(cfg2.overrides().grpc_service()));
}

// When merging two configurations, unset grpc_service is equivalent to no override.
TEST(OverrideTest, GrpcServiceNonOverride) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");
  ExtProcPerRoute cfg2;
  // Leave cfg2.grpc_service unset.
  FilterConfigPerRoute route1(cfg1);
  FilterConfigPerRoute route2(cfg2);
  route1.merge(route2);
  ASSERT_TRUE(route1.grpcService().has_value());
  EXPECT_THAT(*route1.grpcService(), ProtoEq(cfg1.overrides().grpc_service()));
}

// Verify that attempts to change headers that are not allowed to be changed
// are ignored and a counter is incremented.
TEST_F(HttpFilterTest, IgnoreInvalidHeaderMutations) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& header_resp) {
        auto headers_mut = header_resp.mutable_response()->mutable_header_mutation();
        auto add1 = headers_mut->add_set_headers();
        // Not allowed to change the "host" header by default
        add1->mutable_header()->set_key("Host");
        add1->mutable_header()->set_value("wrong:1234");
        // Not allowed to remove x-envoy headers by default.
        headers_mut->add_remove_headers("x-envoy-special-thing");
      });

  // The original headers should not have been modified now.
  Http::TestRequestHeaderMapImpl expected{
      {":path", "/"},
      {":method", "POST"},
      {":scheme", "http"},
      {"host", "host"},
  };
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected));

  stream_callbacks_->onGrpcClose();
  filter_->onDestroy();

  EXPECT_EQ(2, config_->stats().rejected_header_mutations_.value());
}

// Verify that attempts to change headers that are not allowed to be changed
// can be configured to cause a request failure.
TEST_F(HttpFilterTest, FailOnInvalidHeaderMutations) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  mutation_rules:
    disallow_is_error:
      value: true
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, _, _, Eq(absl::nullopt), _))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           Unused, Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto headers_mut =
      resp1->mutable_request_headers()->mutable_response()->mutable_header_mutation();
  auto add1 = headers_mut->add_set_headers();
  // Not allowed to change the "host" header by default
  add1->mutable_header()->set_key("Host");
  add1->mutable_header()->set_value("wrong:1234");
  // Not allowed to remove x-envoy headers by default.
  headers_mut->add_remove_headers("x-envoy-special-thing");
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  EXPECT_TRUE(immediate_response_headers.empty());
  stream_callbacks_->onGrpcClose();

  filter_->onDestroy();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
