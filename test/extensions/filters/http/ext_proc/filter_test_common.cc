#include "test/extensions/filters/http/ext_proc/filter_test_common.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/sidestream_watermark.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ::Envoy::Http::FilterDataStatus;
using ::Envoy::Http::FilterHeadersStatus;
using ::Envoy::Http::FilterTrailersStatus;
using ::Envoy::Http::LowerCaseString;
using ::envoy::service::ext_proc::v3::BodyResponse;
using ::envoy::service::ext_proc::v3::HeadersResponse;
using ::envoy::service::ext_proc::v3::HttpBody;
using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::HttpTrailers;
using ::envoy::service::ext_proc::v3::TrailersResponse;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtMost;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Unused;

using namespace std::chrono_literals;

static constexpr absl::string_view filter_config_name = "envoy.filters.http.ext_proc";
static constexpr uint32_t BufferSize = 100000;

void HttpFilterTest::initialize(std::string&& yaml, bool is_upstream_filter) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.ext_proc_stream_close_optimization", "true"}});
  client_ = std::make_unique<MockClient>();
  route_ = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*client_, start(_, _, _, _)).WillOnce(Invoke(this, &HttpFilterTest::doStart));
  EXPECT_CALL(encoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_CALL(decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(Return(route_));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(dynamic_metadata_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Invoke(this, &HttpFilterTest::doSetDynamicMetadata));

  EXPECT_CALL(decoder_callbacks_, connection())
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(encoder_callbacks_, connection())
      .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));

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
  auto builder_ptr = Envoy::Extensions::Filters::Common::Expr::createBuilder({});
  builder_ = std::make_shared<Envoy::Extensions::Filters::Common::Expr::BuilderInstance>(
      std::move(builder_ptr));
  config_ = std::make_shared<FilterConfig>(proto_config, 200ms, 10000, *stats_store_.rootScope(),
                                           "", is_upstream_filter, builder_, factory_context_);
  filter_ = std::make_unique<Filter>(config_, std::move(client_));
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(Return(BufferSize));
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(BufferSize));
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
}

void HttpFilterTest::initializeTestSendAll() {
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

void HttpFilterTest::TearDown() {
  // This will fail if, at the end of the test, we left any timers enabled.
  // (This particular test suite does not actually let timers expire,
  // although other test suites do.)
  EXPECT_TRUE(allTimersDisabled());
}

bool HttpFilterTest::allTimersDisabled() {
  for (auto* t : timers_) {
    if (t->enabled_) {
      return false;
    }
  }
  return true;
}

ExternalProcessorStreamPtr
HttpFilterTest::doStart(ExternalProcessorCallbacks& callbacks,
                        const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                        const Envoy::Http::AsyncClient::StreamOptions&,
                        Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&) {
  if (final_expected_grpc_service_.has_value()) {
    EXPECT_TRUE(TestUtility::protoEqual(final_expected_grpc_service_.value(),
                                        config_with_hash_key.config()));
  }

  stream_callbacks_ = &callbacks;

  auto stream = std::make_unique<NiceMock<MockStream>>();
  // We never send with the "close" flag set
  EXPECT_CALL(*stream, send(_, false)).WillRepeatedly(Invoke(this, &HttpFilterTest::doSend));

  EXPECT_CALL(*stream, streamInfo()).WillRepeatedly(ReturnRef(async_client_stream_info_));

  // Either close or graceful close will be called.
  EXPECT_CALL(*stream, close())
      .Times(AtMost(1))
      .WillRepeatedly(Invoke(this, &HttpFilterTest::doSendClose));
  EXPECT_CALL(*stream, halfCloseAndDeleteOnRemoteClose())
      .Times(AtMost(1))
      .WillRepeatedly(Invoke(this, &HttpFilterTest::doSendClose));

  return stream;
}

void HttpFilterTest::setUpDecodingBuffering(Buffer::Instance& buf, bool expect_modification) {
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&buf));
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillRepeatedly(Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
  if (expect_modification) {
    EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer(_))
        .WillOnce(
            Invoke([&buf](std::function<void(Buffer::Instance&)> callback) { callback(buf); }));
  }
}

void HttpFilterTest::setUpEncodingBuffering(Buffer::Instance& buf, bool expect_modification) {
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(&buf));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
      .WillRepeatedly(Invoke([&buf](Buffer::Instance& new_chunk, Unused) { buf.add(new_chunk); }));
  if (expect_modification) {
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer(_))
        .WillOnce(
            Invoke([&buf](std::function<void(Buffer::Instance&)> callback) { callback(buf); }));
  }
}

void HttpFilterTest::setUpDecodingWatermarking(bool& watermarked) {
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

void HttpFilterTest::setUpEncodingWatermarking(bool& watermarked) {
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

void HttpFilterTest::processRequestHeaders(
    bool buffering_data,
    absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
        cb) {
  ASSERT_TRUE(last_request_.has_request_headers());
  const auto& headers = last_request_.request_headers();

  auto response = std::make_unique<ProcessingResponse>();
  auto* headers_response = response->mutable_request_headers();
  if (cb) {
    (*cb)(headers, *response, *headers_response);
  }

  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }

  EXPECT_FALSE(last_request_.observability_mode());
  if (!buffering_data) {
    EXPECT_CALL(decoder_callbacks_, continueDecoding());
  }
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  stream_callbacks_->onReceiveMessage(std::move(response));
}

void HttpFilterTest::processResponseHeaders(
    bool buffering_data,
    absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
        cb) {
  ASSERT_TRUE(last_request_.has_response_headers());
  const auto& headers = last_request_.response_headers();
  auto response = std::make_unique<ProcessingResponse>();
  auto* headers_response = response->mutable_response_headers();
  if (cb) {
    (*cb)(headers, *response, *headers_response);
  }

  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }

  EXPECT_FALSE(last_request_.observability_mode());
  if (!buffering_data) {
    EXPECT_CALL(encoder_callbacks_, continueEncoding());
  }
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  stream_callbacks_->onReceiveMessage(std::move(response));
}

void HttpFilterTest::processResponseHeadersAfterTrailer(
    absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
        cb) {
  HttpHeaders headers;
  auto response = std::make_unique<ProcessingResponse>();
  auto* headers_response = response->mutable_response_headers();
  if (cb) {
    (*cb)(headers, *response, *headers_response);
  }
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  stream_callbacks_->onReceiveMessage(std::move(response));
}

void HttpFilterTest::processRequestBody(
    absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
    bool should_continue, const std::chrono::microseconds latency) {
  ASSERT_TRUE(last_request_.has_request_body());

  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }

  EXPECT_FALSE(last_request_.observability_mode());
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

void HttpFilterTest::processResponseBody(
    absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
    bool should_continue) {
  ASSERT_TRUE(last_request_.has_response_body());

  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }

  EXPECT_FALSE(last_request_.observability_mode());
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

void HttpFilterTest::processResponseBodyHelper(absl::string_view data,
                                               Buffer::OwnedImpl& want_response_body,
                                               bool end_of_stream, bool should_continue) {
  processResponseBody(
      [&](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
        auto* streamed_response =
            resp.mutable_response()->mutable_body_mutation()->mutable_streamed_response();
        streamed_response->set_end_of_stream(end_of_stream);
        streamed_response->set_body(data);
        want_response_body.add(data);
      },
      should_continue);
}

void HttpFilterTest::processResponseBodyStreamedAfterTrailer(
    absl::string_view data, Buffer::OwnedImpl& want_response_body) {
  auto response = std::make_unique<ProcessingResponse>();
  auto* body_response = response->mutable_response_body();
  auto* streamed_response =
      body_response->mutable_response()->mutable_body_mutation()->mutable_streamed_response();
  streamed_response->set_body(data);
  want_response_body.add(data);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  stream_callbacks_->onReceiveMessage(std::move(response));
}

void HttpFilterTest::processRequestTrailers(
    absl::optional<std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
        cb,
    bool should_continue) {
  ASSERT_TRUE(last_request_.has_request_trailers());

  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }

  EXPECT_FALSE(last_request_.observability_mode());
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

void HttpFilterTest::processResponseTrailers(
    absl::optional<std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
        cb,
    bool should_continue) {
  ASSERT_TRUE(last_request_.has_response_trailers());
  if (observability_mode_) {
    EXPECT_TRUE(last_request_.observability_mode());
    return;
  }
  EXPECT_FALSE(last_request_.observability_mode());

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

const ExtProcLoggingInfo::GrpcCalls&
HttpFilterTest::getGrpcCalls(const envoy::config::core::v3::TrafficDirection traffic_direction) {
  // The number of processor grpc calls made in the encoding and decoding path.
  const ExtProcLoggingInfo::GrpcCalls& grpc_calls =
      stream_info_.filterState()
          ->getDataReadOnly<Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
              filter_config_name)
          ->grpcCalls(traffic_direction);
  return grpc_calls;
}

void HttpFilterTest::checkGrpcCall(const ExtProcLoggingInfo::GrpcCall call,
                                   const std::chrono::microseconds latency,
                                   const Grpc::Status::GrpcStatus call_status) {
  EXPECT_TRUE(call.latency_ == latency);
  EXPECT_TRUE(call.call_status_ == call_status);
}

void HttpFilterTest::checkGrpcCallBody(const ExtProcLoggingInfo::GrpcCallBody call,
                                       const uint32_t call_count,
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

void HttpFilterTest::checkGrpcCallHeaderOnlyStats(
    const envoy::config::core::v3::TrafficDirection traffic_direction,
    const Grpc::Status::GrpcStatus call_status) {
  auto& grpc_calls = getGrpcCalls(traffic_direction);
  EXPECT_TRUE(grpc_calls.header_stats_ != nullptr);
  checkGrpcCall(*grpc_calls.header_stats_, std::chrono::microseconds(10), call_status);
  EXPECT_TRUE(grpc_calls.trailer_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls.body_stats_ == nullptr);
}

void HttpFilterTest::checkGrpcCallStatsAll(
    const envoy::config::core::v3::TrafficDirection traffic_direction,
    const uint32_t body_chunk_number, const Grpc::Status::GrpcStatus body_call_status,
    const bool trailer_stats) {
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

void HttpFilterTest::expectNoGrpcCall(
    const envoy::config::core::v3::TrafficDirection traffic_direction) {
  auto& grpc_calls = getGrpcCalls(traffic_direction);
  EXPECT_TRUE(grpc_calls.header_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls.trailer_stats_ == nullptr);
  EXPECT_TRUE(grpc_calls.body_stats_ == nullptr);
}

void HttpFilterTest::sendChunkRequestData(const uint32_t chunk_number, const bool send_grpc) {
  for (uint32_t i = 0; i < chunk_number; i++) {
    Buffer::OwnedImpl req_data("foo");
    EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
    if (send_grpc) {
      processRequestBody(absl::nullopt, false);
    }
  }
}

void HttpFilterTest::sendChunkResponseData(const uint32_t chunk_number, const bool send_grpc) {
  for (uint32_t i = 0; i < chunk_number; i++) {
    Buffer::OwnedImpl resp_data("bar");
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
    if (send_grpc) {
      processResponseBody(absl::nullopt, false);
    }
  }
}

void HttpFilterTest::streamingSmallChunksWithBodyMutation(bool empty_last_chunk,
                                                          bool mutate_last_chunk) {
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
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));
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

void HttpFilterTest::expectFilterState(const Envoy::Protobuf::Struct& expected_metadata) {
  const auto* filterState =
      stream_info_.filterState()
          ->getDataReadOnly<Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
              filter_config_name);
  const Envoy::Protobuf::Struct& loggedMetadata = filterState->filterMetadata();
  EXPECT_THAT(loggedMetadata, ProtoEq(expected_metadata));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
