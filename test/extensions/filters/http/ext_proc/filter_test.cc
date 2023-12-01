#include <algorithm>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using ::envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using ::envoy::service::ext_proc::v3::BodyResponse;
using ::envoy::service::ext_proc::v3::CommonResponse;
using ::envoy::service::ext_proc::v3::HeadersResponse;
using ::envoy::service::ext_proc::v3::HttpBody;
using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::HttpTrailers;
using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ::envoy::service::ext_proc::v3::ProcessingResponse;
using ::envoy::service::ext_proc::v3::TrailersResponse;

using ::Envoy::Http::Filter1xxHeadersStatus;
using ::Envoy::Http::FilterChainFactoryCallbacks;
using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterFactoryCb;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::LowerCaseString;
using ::Envoy::Http::MockStreamDecoderFilter;
using ::Envoy::Http::MockStreamEncoderFilter;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::RequestHeaderMapPtr;
using ::Envoy::Http::ResponseHeaderMap;
using ::Envoy::Http::ResponseHeaderMapPtr;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::TestResponseTrailerMapImpl;

using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Unused;

using namespace std::chrono_literals;

static const uint32_t BufferSize = 100000;
static const std::string filter_config_name = "scooby.dooby.doo";

// These tests are all unit tests that directly drive an instance of the
// ext_proc filter and verify the behavior using mocks.

class HttpFilterTest : public testing::Test {
protected:
  void initialize(std::string&& yaml) {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.send_header_raw_value", "false"}});
    client_ = std::make_unique<MockClient>();
    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(*client_, start(_, _, _)).WillOnce(Invoke(this, &HttpFilterTest::doStart));
    EXPECT_CALL(encoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(route_));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

    // Pointing dispatcher_.time_system_ to a SimulatedTimeSystem object.
    test_time_ = new Envoy::Event::SimulatedTimeSystem();
    dispatcher_.time_system_.reset(test_time_);

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
    EXPECT_CALL(decoder_callbacks_, filterConfigName()).WillRepeatedly(Return(filter_config_name));

    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    config_ =
        std::make_shared<FilterConfig>(proto_config, 200ms, 10000, *stats_store_.rootScope(), "");
    filter_ = std::make_unique<Filter>(config_, std::move(client_), proto_config.grpc_service());
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(Return(BufferSize));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(BufferSize));
    HttpTestUtility::addDefaultHeaders(request_headers_);
    request_headers_.setMethod("POST");
  }

  void initializeTestSendAll() {
    initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SEND"
    response_trailer_mode: "SEND"
  )EOF");
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

  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks,
                                     const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                                     testing::Unused) {
    if (final_expected_grpc_service_.has_value()) {
      EXPECT_TRUE(TestUtility::protoEqual(final_expected_grpc_service_.value(),
                                          config_with_hash_key.config()));
      std::cout << final_expected_grpc_service_.value().DebugString();
      std::cout << config_with_hash_key.config().DebugString();
    }

    stream_callbacks_ = &callbacks;

    auto stream = std::make_unique<MockStream>();
    // We never send with the "close" flag set
    EXPECT_CALL(*stream, send(_, false)).WillRepeatedly(Invoke(this, &HttpFilterTest::doSend));

    EXPECT_CALL(*stream, streamInfo()).WillRepeatedly(ReturnRef(async_client_stream_info_));

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
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
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
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // Expect a request_body request, and send back a valid response
  void processRequestBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true,
      const std::chrono::microseconds latency = std::chrono::microseconds(10)) {
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
    test_time_->advanceTimeWait(latency);
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
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void processRequestTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_request_trailers());
    const auto& trailers = last_request_.request_trailers();
    auto response = std::make_unique<ProcessingResponse>();
    auto* trailers_response = response->mutable_request_trailers();
    if (cb) {
      (*cb)(trailers, *response, *trailers_response);
    }
    if (should_continue) {
      EXPECT_CALL(decoder_callbacks_, continueDecoding());
    }
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void processResponseTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true) {
    EXPECT_FALSE(last_request_.async_mode());
    ASSERT_TRUE(last_request_.has_response_trailers());
    const auto& trailers = last_request_.response_trailers();
    auto response = std::make_unique<ProcessingResponse>();
    auto* trailers_response = response->mutable_response_trailers();
    if (cb) {
      (*cb)(trailers, *response, *trailers_response);
    }
    if (should_continue) {
      EXPECT_CALL(encoder_callbacks_, continueEncoding());
    }
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  // Get the gRPC call stats data from the filter state.
  const ExtProcLoggingInfo::GrpcCalls&
  getGrpcCalls(const envoy::config::core::v3::TrafficDirection traffic_direction) {
    // The number of processor grpc calls made in the encoding and decoding path.
    const ExtProcLoggingInfo::GrpcCalls& grpc_calls =
        stream_info_.filterState()
            ->getDataReadOnly<
                Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
                filter_config_name)
            ->grpcCalls(traffic_direction);
    return grpc_calls;
  }

  // Check gRPC call stats for headers and trailers.
  void checkGrpcCall(const ExtProcLoggingInfo::GrpcCall call,
                     const std::chrono::microseconds latency,
                     const Grpc::Status::GrpcStatus call_status) {
    EXPECT_TRUE(call.latency_ == latency);
    EXPECT_TRUE(call.call_status_ == call_status);
  }

  // Check gRPC call stats for body.
  void checkGrpcCallBody(const ExtProcLoggingInfo::GrpcCallBody call, const uint32_t call_count,
                         const Grpc::Status::GrpcStatus call_status,
                         const std::chrono::microseconds total_latency,
                         const std::chrono::microseconds max_latency,
                         const std::chrono::microseconds min_latency) {
    EXPECT_TRUE(call.call_count_ == call_count);
    EXPECT_TRUE(call.last_call_status_ == call_status);
    EXPECT_TRUE(call.total_latency_ == total_latency);
    EXPECT_TRUE(call.max_latency_ == max_latency);
    EXPECT_TRUE(call.min_latency_ == min_latency);
  }

  // Verify gRPC calls only happened on headers.
  void
  checkGrpcCallHeaderOnlyStats(const envoy::config::core::v3::TrafficDirection traffic_direction,
                               const Grpc::Status::GrpcStatus call_status = Grpc::Status::Ok) {
    auto& grpc_calls = getGrpcCalls(traffic_direction);
    EXPECT_TRUE(grpc_calls.header_stats_ != nullptr);
    checkGrpcCall(*grpc_calls.header_stats_, std::chrono::microseconds(10), call_status);
    EXPECT_TRUE(grpc_calls.trailer_stats_ == nullptr);
    EXPECT_TRUE(grpc_calls.body_stats_ == nullptr);
  }

  // Verify gRPC calls for  headers, body, and trailer.
  void checkGrpcCallStatsAll(const envoy::config::core::v3::TrafficDirection traffic_direction,
                             const uint32_t body_chunk_number,
                             const Grpc::Status::GrpcStatus body_call_status = Grpc::Status::Ok,
                             const bool trailer_stats = true) {
    auto& grpc_calls = getGrpcCalls(traffic_direction);
    EXPECT_TRUE(grpc_calls.header_stats_ != nullptr);
    checkGrpcCall(*grpc_calls.header_stats_, std::chrono::microseconds(10), Grpc::Status::Ok);

    if (trailer_stats) {
      EXPECT_TRUE(grpc_calls.trailer_stats_ != nullptr);
      checkGrpcCall(*grpc_calls.trailer_stats_, std::chrono::microseconds(10), Grpc::Status::Ok);
    } else {
      EXPECT_TRUE(grpc_calls.trailer_stats_ == nullptr);
    }

    EXPECT_TRUE(grpc_calls.body_stats_ != nullptr);
    checkGrpcCallBody(*grpc_calls.body_stats_, body_chunk_number, body_call_status,
                      std::chrono::microseconds(10) * body_chunk_number,
                      std::chrono::microseconds(10), std::chrono::microseconds(10));
  }

  // Verify no gRPC call happened.
  void expectNoGrpcCall(const envoy::config::core::v3::TrafficDirection traffic_direction) {
    auto& grpc_calls = getGrpcCalls(traffic_direction);
    EXPECT_TRUE(grpc_calls.header_stats_ == nullptr);
    EXPECT_TRUE(grpc_calls.trailer_stats_ == nullptr);
    EXPECT_TRUE(grpc_calls.body_stats_ == nullptr);
  }

  void sendChunkRequestData(const uint32_t chunk_number, const bool send_grpc) {
    for (uint32_t i = 0; i < chunk_number; i++) {
      Buffer::OwnedImpl req_data("foo");
      EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
      if (send_grpc) {
        processRequestBody(absl::nullopt, false);
      }
    }
  }

  void sendChunkResponseData(const uint32_t chunk_number, const bool send_grpc) {
    for (uint32_t i = 0; i < chunk_number; i++) {
      Buffer::OwnedImpl resp_data("bar");
      EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
      if (send_grpc) {
        processResponseBody(absl::nullopt, false);
      }
    }
  }

  void StreamingSmallChunksWithBodyMutation(bool empty_last_chunk, bool mutate_last_chunk) {
    initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_body_mode: "NONE"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

    EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

    Buffer::OwnedImpl first_chunk("foo");
    EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, false));
    EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

    response_headers_.addCopy(LowerCaseString(":status"), "200");
    response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
    EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

    Buffer::OwnedImpl want_response_body;
    Buffer::OwnedImpl got_response_body;
    EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
        .WillRepeatedly(Invoke([&got_response_body](Buffer::Instance& data, Unused) {
          got_response_body.move(data);
        }));
    uint32_t chunk_number = 3;
    for (uint32_t i = 0; i < chunk_number; i++) {
      Buffer::OwnedImpl resp_data(std::to_string(i));
      EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
      processResponseBody(
          [i, &want_response_body](const HttpBody& body, ProcessingResponse&, BodyResponse& resp) {
            auto* body_mut = resp.mutable_response()->mutable_body_mutation();
            body_mut->set_body(body.body() + " " + std::to_string(i) + " ");
            want_response_body.add(body.body() + " " + std::to_string(i) + " ");
          },
          false);
    }

    std::string last_chunk_str = "";
    Buffer::OwnedImpl resp_data;
    if (!empty_last_chunk) {
      last_chunk_str = std::to_string(chunk_number);
    }
    resp_data.add(last_chunk_str);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));
    if (mutate_last_chunk) {
      processResponseBody(
          [&chunk_number, &want_response_body](const HttpBody& body, ProcessingResponse&,
                                               BodyResponse& resp) {
            auto* body_mut = resp.mutable_response()->mutable_body_mutation();
            body_mut->set_body(body.body() + " " + std::to_string(chunk_number) + " ");
            want_response_body.add(body.body() + " " + std::to_string(chunk_number) + " ");
          },
          true);
    } else {
      processResponseBody(absl::nullopt, true);
      want_response_body.add(last_chunk_str);
    }

    EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
    filter_->onDestroy();

    EXPECT_EQ(1, config_->stats().streams_started_.value());
    EXPECT_EQ(4, config_->stats().stream_msgs_sent_.value());
    EXPECT_EQ(4, config_->stats().stream_msgs_received_.value());
    EXPECT_EQ(1, config_->stats().streams_closed_.value());
  }

  // The metadata configured as part of ext_proc filter should be in the filter state.
  // In addition, bytes sent/received should also be stored.
  void expectFilterState(const Envoy::ProtobufWkt::Struct& expected_metadata) {
    const auto* filterState =
        stream_info_.filterState()
            ->getDataReadOnly<
                Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
                filter_config_name);
    const Envoy::ProtobufWkt::Struct& loggedMetadata = filterState->filterMetadata();
    EXPECT_THAT(loggedMetadata, ProtoEq(expected_metadata));
  }

  absl::optional<envoy::config::core::v3::GrpcService> final_expected_grpc_service_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  std::unique_ptr<MockClient> client_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  ProcessingRequest last_request_;
  bool server_closed_stream_ = false;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  std::shared_ptr<Filter> filter_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<::Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<::Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Router::RouteConstSharedPtr route_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<StreamInfo::MockStreamInfo> async_client_stream_info_;
  TestRequestHeaderMapImpl request_headers_;
  TestResponseHeaderMapImpl response_headers_;
  TestRequestTrailerMapImpl request_trailers_;
  TestResponseTrailerMapImpl response_trailers_;
  std::vector<Event::MockTimer*> timers_;
  TestScopedRuntime scoped_runtime_;
  Envoy::Event::SimulatedTimeSystem* test_time_;
};

// Using the default configuration, test the filter with a processor that
// replies to the request_headers message with an empty response
TEST_F(HttpFilterTest, SimplestPost) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  filter_metadata:
    scooby: "doo"
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

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
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
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND);
  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::OUTBOUND);

  Envoy::ProtobufWkt::Struct filter_metadata;
  (*filter_metadata.mutable_fields())["scooby"].set_string_value("doo");
  expectFilterState(filter_metadata);
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
  TestRequestHeaderMapImpl expected{{":path", "/"},
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
    TestRequestHeaderMapImpl expected_response{
        {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
    EXPECT_THAT(response_headers.headers(), HeaderProtosEqual(expected_response));

    auto* resp_headers_mut = header_resp.mutable_response()->mutable_header_mutation();
    auto* resp_add1 = resp_headers_mut->add_set_headers();
    resp_add1->mutable_header()->set_key("x-new-header");
    resp_add1->mutable_header()->set_value("new");
  });

  // We should now have changed the original header a bit
  TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
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
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got_a_bad_request"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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

  TestResponseHeaderMapImpl expected_response_headers{
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

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND);
  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);

  expectFilterState(Envoy::ProtobufWkt::Struct());
}

TEST_F(HttpFilterTest, PostAndRespondImmediatelyWithDisabledConfig) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  disable_immediate_response: true
  )EOF");

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp1->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::BadRequest);
  immediate_response->set_body("Bad request");
  immediate_response->set_details("Got a bad request");
  auto* immediate_headers = immediate_response->mutable_headers();
  auto* hdr1 = immediate_headers->add_set_headers();
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_value("text/plain");
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(filter_->decodeData(req_data, true), FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), FilterTrailersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), FilterHeadersStatus::Continue);
  filter_->onDestroy();

  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 1);
  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 0);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
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

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::BadRequest, "Bad request", _,
                                                 Eq(absl::nullopt), "Got_a_bad_request"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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
  std::string request_body = "Replaced!";
  int request_body_length = request_body.size();
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), request_body_length);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(true, absl::nullopt);
  EXPECT_EQ(request_headers_.getContentLengthValue(), absl::StrCat(request_body_length));

  Buffer::OwnedImpl req_data;
  TestUtility::feedBufferWithRandomCharacters(req_data, 100);
  Buffer::OwnedImpl buffered_data;
  setUpDecodingBuffering(buffered_data, true);

  // Testing the case where we just have one chunk of data and it is buffered.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_data, true));
  processRequestBody([&buffered_data, &request_body](const HttpBody& req_body, ProcessingResponse&,
                                                     BodyResponse& body_resp) {
    EXPECT_TRUE(req_body.end_of_stream());
    EXPECT_EQ(100, req_body.body().size());
    EXPECT_EQ(req_body.body(), buffered_data.toString());
    auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
    body_mut->set_body(request_body);
  });
  // Expect that the original buffer is replaced.
  EXPECT_EQ(request_body, buffered_data.toString());
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  std::string response_body = "bar";
  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), absl::StrCat(response_body.size()));

  EXPECT_EQ(Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data;
  resp_data.add(response_body);
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
  // when end_stream is true, we still do buffering.
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_4, true));
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

  EXPECT_EQ(Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
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

  EXPECT_EQ(Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
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
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(req_data, false));
  upstream_request_body.move(req_data);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_data, true));
  upstream_request_body.move(empty_data);

  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, true))
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
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, true))
      .WillRepeatedly(Invoke([&downstream_response_body](Buffer::Instance& data, Unused) {
        downstream_response_body.move(data);
      }));

  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data_1, false));
  downstream_response_body.move(resp_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data_2, false));
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
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_2, true));
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

// Streaming sends body with small chunks.
TEST_F(HttpFilterTest, StreamingDataSmallChunk) {
  initializeTestSendAll();

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  const uint32_t chunk_number = 20;
  sendChunkRequestData(chunk_number, true);
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  processRequestTrailers(absl::nullopt, true);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);
  sendChunkResponseData(chunk_number * 2, true);
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
  processResponseTrailers(absl::nullopt, false);
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  // Total gRPC messages include two headers and two trailers on top of the req/resp chunk data.
  uint32_t total_msg = 3 * chunk_number + 4;
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::INBOUND, chunk_number);
  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::OUTBOUND, 2 * chunk_number);
}

TEST_F(HttpFilterTest, StreamingBodyMutateLastEmptyChunk) {
  StreamingSmallChunksWithBodyMutation(true, true);
}

TEST_F(HttpFilterTest, StreamingBodyNotMutateLastEmptyChunk) {
  StreamingSmallChunksWithBodyMutation(true, false);
}

TEST_F(HttpFilterTest, StreamingBodyMutateLastChunk) {
  StreamingSmallChunksWithBodyMutation(false, true);
}

TEST_F(HttpFilterTest, StreamingBodyNotMutateLastChunk) {
  StreamingSmallChunksWithBodyMutation(false, false);
}

// gRPC call fails when streaming sends small chunk request data.
TEST_F(HttpFilterTest, StreamingSendRequestDataGrpcFail) {
  initializeTestSendAll();

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  Buffer::OwnedImpl req_data("foo");
  const uint32_t chunk_number = 20;
  sendChunkRequestData(chunk_number, true);
  // When sends one more chunk of data, gRPC call fails.
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  // Oh no! The remote server had a failure!
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  // Sending another chunk of data. No more gRPC call.
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  sendChunkResponseData(chunk_number * 2, false);
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  // Total gRPC messages include one header and 21 chunk of data.
  uint32_t total_msg = chunk_number + 2;
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(total_msg - 1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());

  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::INBOUND, chunk_number + 1,
                        Grpc::Status::Internal, false);
  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);
}

// gRPC call fails when streaming sends small chunk response data.
TEST_F(HttpFilterTest, StreamingSendResponseDataGrpcFail) {
  initializeTestSendAll();

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  const uint32_t chunk_number = 20;
  sendChunkRequestData(chunk_number, true);
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  processRequestTrailers(absl::nullopt, true);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);
  sendChunkResponseData(chunk_number / 2, true);
  // When sends one more chunk of data, gRPC call fails.
  Buffer::OwnedImpl resp_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);
  // Sending 40 more chunks of data. No more gRPC calls.
  sendChunkRequestData(chunk_number * 2, false);

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
  EXPECT_TRUE(immediate_response_headers.empty());

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  // Total gRPC messages include two headers, one trailers and 20 request chunk and 11 response
  // chunk data.
  uint32_t total_msg = 3 + chunk_number + chunk_number / 2 + 1;
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(total_msg - 1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());

  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::INBOUND, chunk_number);
  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::OUTBOUND, chunk_number / 2 + 1,
                        Grpc::Status::Internal, false);
}

// Grpc fails when sending request trailer message.
TEST_F(HttpFilterTest, GrpcFailOnRequestTrailer) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_trailer_mode: "SEND"
  )EOF");

  EXPECT_FALSE(config_->failureModeAllow());

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));

  filter_->onDestroy();
  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(0, config_->stats().streams_closed_.value());
  EXPECT_EQ(1, config_->stats().streams_failed_.value());

  auto& grpc_calls_in = getGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND);
  EXPECT_TRUE(grpc_calls_in.header_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls_in.trailer_stats_ != nullptr);
  checkGrpcCall(*grpc_calls_in.trailer_stats_, std::chrono::microseconds(10),
                Grpc::Status::Internal);
  EXPECT_TRUE(grpc_calls_in.body_stats_ == nullptr);

  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);
}

// Sending gRPC calls with random latency To test max and min latency update logic.
TEST_F(HttpFilterTest, StreamingSendDataRandomGrpcLatency) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SKIP"
    request_body_mode: "STREAMED"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const uint32_t chunk_number = 5;
  Buffer::OwnedImpl req_data("foo");
  // Latency 50 80 60 30 100.
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(50));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(80));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(60));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(30));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(100));

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  uint32_t total_msg = chunk_number;
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(total_msg, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(0, config_->stats().streams_failed_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  auto& grpc_calls_in = getGrpcCalls(envoy::config::core::v3::TrafficDirection::INBOUND);
  EXPECT_TRUE(grpc_calls_in.header_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls_in.trailer_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls_in.body_stats_ != nullptr);
  checkGrpcCallBody(*grpc_calls_in.body_stats_, chunk_number, Grpc::Status::Ok,
                    std::chrono::microseconds(320), std::chrono::microseconds(100),
                    std::chrono::microseconds(30));

  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);
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
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
  )EOF");

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  request_headers_.addCopy(LowerCaseString("content-length"), 100);

  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);
  // Test content-length header is removed in request in streamed mode.
  EXPECT_EQ(request_headers_.ContentLength(), nullptr);

  bool decoding_watermarked = false;
  setUpDecodingWatermarking(decoding_watermarked);

  Buffer::OwnedImpl want_request_body;
  Buffer::OwnedImpl got_request_body;
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, true))
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
  // Test content-length header is removed in response in streamed mode.
  EXPECT_EQ(response_headers_.ContentLength(), nullptr);

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

  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::INBOUND, 1, Grpc::Status::Ok,
                        false);
  checkGrpcCallStatsAll(envoy::config::core::v3::TrafficDirection::OUTBOUND, 6, Grpc::Status::Ok,
                        false);
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
  EXPECT_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, true))
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
  allow_mode_override: true
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
  for (int i = 0; i < 3; i++) {
    processResponseBody(absl::nullopt, false);
  }

  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, true))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  // Close the stream
  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);

  // At this point, the whole body should have been processed including things
  // that were rejected.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(7, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(7, config_->stats().stream_msgs_received_.value());
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
  allow_mode_override: true
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

  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, true))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, true));
  got_response_body.move(resp_chunk);

  processResponseBody(absl::nullopt, false);
  processResponseBody(absl::nullopt, false);
  processResponseBody(absl::nullopt, true);

  // At this point, the whole body should have been processed including things
  // that were rejected.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(6, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(6, config_->stats().stream_msgs_received_.value());
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

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::OK, "", _, Eq(absl::nullopt), ""))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::Forbidden, "", _, Eq(999), ""))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  // Oh no! The remote server had a failure!
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND,
                               Grpc::Status::Internal);
  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);
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
  processRequestHeaders(false, absl::nullopt);

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _,
                                                 Eq(absl::nullopt), "ext_proc_error_gRPC_error_13"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND);
  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::OUTBOUND,
                               Grpc::Status::Internal);
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
  processRequestHeaders(false, absl::nullopt);

  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  TestRequestHeaderMapImpl final_expected_response{
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

// Initial configuration is send everything, but after request header processing,
// override the processing mode to disable the rest.
TEST_F(HttpFilterTest, ProcessingModeOverrideResponseHeaders) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  allow_mode_override: true
  processing_mode:
    request_body_mode: "STREAMED"
    response_body_mode: "STREAMED"
    request_trailer_mode: "SEND"
    response_trailer_mode: "SEND"
  )EOF");
  EXPECT_EQ(filter_->config().allowModeOverride(), true);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  processRequestHeaders(false,
                        [](const HttpHeaders&, ProcessingResponse& response, HeadersResponse&) {
                          auto mode = response.mutable_mode_override();
                          mode->set_request_body_mode(ProcessingMode::NONE);
                          mode->set_request_trailer_mode(ProcessingMode::SKIP);
                          mode->set_response_header_mode(ProcessingMode::SKIP);
                          mode->set_response_body_mode(ProcessingMode::NONE);
                          mode->set_response_trailer_mode(ProcessingMode::SKIP);
                        });
  Buffer::OwnedImpl first_chunk("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(first_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  TestRequestHeaderMapImpl final_expected_response{
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

// Leaving the allow_mode_override in filter config to be default, which is false.
// In such case, the mode_override in the response will be ignored.
TEST_F(HttpFilterTest, DisableResponseModeOverride) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
  )EOF");

  EXPECT_EQ(filter_->config().allowModeOverride(), false);
  EXPECT_EQ(filter_->config().processingMode().response_header_mode(), ProcessingMode::SEND);
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

  // Such mode_override is ignored. The response header is still sent to the ext_proc server.
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

  TestRequestHeaderMapImpl final_expected_response{
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

// Test case where all decode*() calling is skipped and configuration is overridden by route
// configuration.
TEST_F(HttpFilterTest, ProcessingModeResponseHeadersOnlyWithoutCallingDecodeHeaders) {
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

  // Route configuration overrides the grpc_service.
  ExtProcPerRoute route_proto;
  route_proto.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");
  FilterConfigPerRoute route_config(route_proto);
  EXPECT_CALL(decoder_callbacks_, traversePerFilterConfig(_))
      .WillOnce(
          testing::Invoke([&](std::function<void(const Router::RouteSpecificFilterConfig&)> cb) {
            cb(route_config);
          }));
  final_expected_grpc_service_.emplace(route_proto.overrides().grpc_service());
  config_with_hash_key_.setConfig(route_proto.overrides().grpc_service());

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, absl::nullopt);

  TestRequestHeaderMapImpl final_expected_response{
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

// Using the default configuration, verify that the "clear_route_cache" flag makes the appropriate
// callback on the filter for inbound traffic when header modifications are also present.
// Also verify it does not make the callback for outbound traffic.
TEST_F(HttpFilterTest, ClearRouteCacheHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  // Call ClearRouteCache() for inbound traffic with header mutation.
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  Buffer::OwnedImpl resp_data("foo");
  Buffer::OwnedImpl buffered_response_data;
  setUpEncodingBuffering(buffered_response_data);

  // There is no ClearRouteCache() call for outbound traffic.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));
  processResponseBody([](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 3);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 3);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

// Verify that the "disable_route_cache_clearing" setting prevents the "clear_route_cache" flag
// from performing route clearing callbacks for inbound traffic when enabled.
TEST_F(HttpFilterTest, ClearRouteCacheDisabledHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  disable_clear_route_cache: true
  )EOF");

  // The ClearRouteCache() call is disabled for inbound traffic.
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  Buffer::OwnedImpl resp_data("foo");
  Buffer::OwnedImpl buffered_response_data;
  setUpEncodingBuffering(buffered_response_data);

  // There is no ClearRouteCache() call for outbound traffic regardless.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));
  processResponseBody([](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 1);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 3);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 3);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

// Using the default configuration, verify that the "clear_route_cache" flag does not preform
// route clearing callbacks for inbound traffic when no header changes are present.
TEST_F(HttpFilterTest, ClearRouteCacheUnchanged) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF");

  // Do not call ClearRouteCache() for inbound traffic without header mutation.
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  Buffer::OwnedImpl resp_data("foo");
  Buffer::OwnedImpl buffered_response_data;
  setUpEncodingBuffering(buffered_response_data);

  // There is no ClearRouteCache() call for outbound traffic regardless.
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_data, true));
  processResponseBody([](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 1);
  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 3);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 3);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
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

  TestRequestHeaderMapImpl expected_request{
      {":scheme", "http"}, {":authority", "host"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected_request));
  EXPECT_EQ(req_buffer.toString(), "Hello, World!");

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);
  EXPECT_EQ(response_headers_.getContentLengthValue(), "200");

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
  TestRequestHeaderMapImpl expected{
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

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, _, _, Eq(absl::nullopt), _))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
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

// Set the HCM max request headers size limit to be 2kb. Test the
// header mutation end result size check works for the trailer response.
TEST_F(HttpFilterTest, ResponseTrailerMutationExceedSizeLimit) {
  TestResponseTrailerMapImpl resp_trailers_({}, 2, 100);
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
    response_trailer_mode: "SEND"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  response_headers_.addCopy(LowerCaseString(":status"), "200");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);
  // Construct a large trailer message to be close to the HCM size limit.
  resp_trailers_.addCopy(LowerCaseString("x-some-trailer"), std::string(1950, 'a'));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(resp_trailers_));
  processResponseTrailers(
      [](const HttpTrailers&, ProcessingResponse&, TrailersResponse& trailer_resp) {
        auto headers_mut = trailer_resp.mutable_header_mutation();
        // The trailer mutation in the response does not exceed the count limit 100 or the
        // size limit 2kb. But the result header map size exceeds the count limit 2kb.
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_header()->set_key("x-new-header-0123456789");
        add1->mutable_header()->set_value("new-header-0123456789");
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header-0123456789");
        add2->mutable_header()->set_value("some-new-header-0123456789");
      },
      false);
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  // The header mutation rejection counter increments.
  EXPECT_EQ(1, config_->stats().rejected_header_mutations_.value());
}

class HttpFilter2Test : public HttpFilterTest,
                        public ::Envoy::Http::HttpConnectionManagerImplMixin {};

// Test proves that when decodeData(data, end_stream=true) is called before request headers response
// is returned, ext_proc filter will buffer the data in the ActiveStream buffer without triggering a
// buffer over high watermark call, which ends in an 413 error return on request path.
TEST_F(HttpFilter2Test, LastDecodeDataCallExceedsStreamBufferLimitWouldJustRaiseHighWatermark) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");
  HttpConnectionManagerImplMixin::setup(false, "fake-server");
  HttpConnectionManagerImplMixin::initial_buffer_limit_ = 10;
  HttpConnectionManagerImplMixin::setUpBufferLimits();

  std::shared_ptr<MockStreamDecoderFilter> mock_filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](::Envoy::Http::FilterChainManager& manager) -> bool {
        // Add ext_proc filter.
        FilterFactoryCb cb = [&](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamDecoderFilter(filter_);
        };
        manager.applyFilterFactoryCb({}, cb);
        // Add the mock-decoder filter.
        FilterFactoryCb mock_filter_cb = [&](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamDecoderFilter(mock_filter);
        };
        manager.applyFilterFactoryCb({}, mock_filter_cb);

        return true;
      }));
  EXPECT_CALL(*mock_filter, decodeHeaders(_, false))
      .WillOnce(Invoke([&](RequestHeaderMap& headers, bool end_stream) {
        // The next decoder filter should be able to see the mutations made by the external server.
        EXPECT_FALSE(end_stream);
        EXPECT_EQ(headers.Path()->value().getStringView(), "/mutated_path/bluh");
        EXPECT_EQ(headers.get(Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView(),
                  "gift-from-external-server");
        mock_filter->callbacks_->sendLocalReply(::Envoy::Http::Code::OK,
                                                "Direct response from mock filter.", nullptr,
                                                absl::nullopt, "");
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, _))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool end_stream) -> void {
        EXPECT_FALSE(end_stream);
        EXPECT_EQ(headers.Status()->value().getStringView(), "200");
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_TRUE(end_stream);
        EXPECT_EQ(data.toString(), "Direct response from mock filter.");
      }));
  // Start the request.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> ::Envoy::Http::Status {
        EXPECT_EQ(data.length(), 5);
        data.drain(5);

        HttpConnectionManagerImplMixin::decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/bluh"}, {":method", "GET"}}};
        HttpConnectionManagerImplMixin::decoder_->decodeHeaders(std::move(headers), false);
        Buffer::OwnedImpl request_body("Definitely more than 10 bytes data.");
        HttpConnectionManagerImplMixin::decoder_->decodeData(request_body, true);
        // Now external server returns the request header response.
        auto response = std::make_unique<ProcessingResponse>();
        auto* headers_response = response->mutable_request_headers();
        auto* hdr =
            headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key("foo");
        hdr->mutable_header()->set_value("gift-from-external-server");
        hdr = headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key(":path");
        hdr->mutable_header()->set_value("/mutated_path/bluh");
        HttpFilterTest::stream_callbacks_->onReceiveMessage(std::move(response));

        return ::Envoy::Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
}

// Test proves that when encodeData(data, end_stream=true) is called before headers response is
// returned, ext_proc filter will buffer the data in the ActiveStream buffer without triggering a
// buffer over high watermark call, which ends in a 500 error on response path.
TEST_F(HttpFilter2Test, LastEncodeDataCallExceedsStreamBufferLimitWouldJustRaiseHighWatermark) {
  // Configure the filter to only pass response headers to ext server.
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
  HttpConnectionManagerImplMixin::setup(false, "fake-server");
  HttpConnectionManagerImplMixin::initial_buffer_limit_ = 10;
  HttpConnectionManagerImplMixin::setUpBufferLimits();

  std::shared_ptr<MockStreamEncoderFilter> mock_encode_filter(
      new NiceMock<MockStreamEncoderFilter>());
  std::shared_ptr<MockStreamDecoderFilter> mock_decode_filter(
      new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*mock_encode_filter, encodeHeaders(_, _))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool end_stream) {
        EXPECT_FALSE(end_stream);
        // The last encode filter will see the mutations from ext server.
        // NOTE: Without raising a high watermark when end_stream is true in onData(), if the stream
        // buffer high watermark reached, a 500 response too large error is raised.
        EXPECT_EQ(headers.Status()->value().getStringView(), "200");
        EXPECT_EQ(headers.get(Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView(),
                  "gift-from-external-server");
        EXPECT_EQ(headers.get(Envoy::Http::LowerCaseString("new_response_header"))[0]
                      ->value()
                      .getStringView(),
                  "bluh");

        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*mock_encode_filter, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool end_stream) {
        EXPECT_TRUE(end_stream);
        EXPECT_EQ(data.toString(),
                  "Direct response from mock filter, Definitely more than 10 bytes data.");
        return FilterDataStatus::Continue;
      }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](::Envoy::Http::FilterChainManager& manager) -> bool {
        // Add the mock-encoder filter.
        FilterFactoryCb mock_encode_filter_cb = [&](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamEncoderFilter(mock_encode_filter);
        };
        manager.applyFilterFactoryCb({}, mock_encode_filter_cb);

        // Add ext_proc filter.
        FilterFactoryCb cb = [&](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamFilter(filter_);
        };
        manager.applyFilterFactoryCb({}, cb);
        // Add the mock-decoder filter.
        FilterFactoryCb mock_decode_filter_cb = [&](FilterChainFactoryCallbacks& callbacks) {
          callbacks.addStreamDecoderFilter(mock_decode_filter);
        };
        manager.applyFilterFactoryCb({}, mock_decode_filter_cb);

        return true;
      }));
  EXPECT_CALL(*mock_decode_filter, decodeHeaders(_, _))
      .WillOnce(Invoke([&](RequestHeaderMap& headers, bool end_stream) {
        EXPECT_TRUE(end_stream);
        EXPECT_EQ(headers.Path()->value().getStringView(), "/bluh");
        // Direct response from decode filter.
        ResponseHeaderMapPtr response_headers{
            new TestResponseHeaderMapImpl{{":status", "200"}, {"foo", "foo-value"}}};
        mock_decode_filter->callbacks_->encodeHeaders(std::move(response_headers), false,
                                                      "filter_direct_response");
        // Send a large body in one shot.
        Buffer::OwnedImpl fake_response(
            "Direct response from mock filter, Definitely more than 10 bytes data.");
        mock_decode_filter->callbacks_->encodeData(fake_response, true);

        // Now return from ext server the response for processing response headers.
        EXPECT_TRUE(last_request_.has_response_headers());
        auto response = std::make_unique<ProcessingResponse>();
        auto* headers_response = response->mutable_response_headers();
        auto* hdr =
            headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key("foo");
        hdr->mutable_header()->set_value("gift-from-external-server");
        hdr = headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_header()->set_key("new_response_header");
        hdr->mutable_header()->set_value("bluh");
        HttpFilterTest::stream_callbacks_->onReceiveMessage(std::move(response));
        return FilterHeadersStatus::StopIteration;
      }));
  // Start the request.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> ::Envoy::Http::Status {
        EXPECT_EQ(data.length(), 5);
        data.drain(5);
        HttpConnectionManagerImplMixin::decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/bluh"}, {":method", "GET"}}};
        HttpConnectionManagerImplMixin::decoder_->decodeHeaders(std::move(headers), true);
        return ::Envoy::Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("hello");
  conn_manager_->onData(fake_input, false);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
