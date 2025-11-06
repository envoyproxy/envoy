#include <algorithm>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3/save_processing_response.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"
#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"
#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response.h"
#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response_factory.h"

#include "test/common/http/common.h"
#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/extensions/filters/http/ext_proc/filter_test_common.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/test_processing_request_modifier.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
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
using ::Envoy::Http::ExternalProcessing::SaveProcessingResponseFactory;
using ::Envoy::Http::ExternalProcessing::SaveProcessingResponseFilterState;

using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Unused;

using namespace std::chrono_literals;

static const uint32_t BufferSize = 100000;
static const std::string filter_config_name = "scooby.dooby.doo";

class HttpFilterTest : public testing::Test {
protected:
  void initialize(std::string&& yaml, bool is_upstream_filter = false) {
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

    // close is idempotent and only called once per filter
    EXPECT_CALL(*stream, close()).WillOnce(Invoke(this, &HttpFilterTest::doSendClose));

    return stream;
  }

  void doSetDynamicMetadata(const std::string& ns, const Protobuf::Struct& val) {
    (*dynamic_metadata_.mutable_filter_metadata())[ns] = val;
  };

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

  // Expect a response_headers request, and send back a valid response
  void processResponseHeaders(
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

  void processResponseHeadersAfterTrailer(
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

  // Expect a request_body request, and send back a valid response
  void processRequestBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true,
      const std::chrono::microseconds latency = std::chrono::microseconds(10)) {
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

  // Expect a request_body request, and send back a valid response
  void processResponseBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true) {
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

  void processResponseBodyHelper(absl::string_view data, Buffer::OwnedImpl& want_response_body,
                                 bool end_of_stream = false, bool should_continue = false) {
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

  void processResponseBodyStreamedAfterTrailer(absl::string_view data,
                                               Buffer::OwnedImpl& want_response_body) {
    auto response = std::make_unique<ProcessingResponse>();
    auto* body_response = response->mutable_response_body();
    auto* streamed_response =
        body_response->mutable_response()->mutable_body_mutation()->mutable_streamed_response();
    streamed_response->set_body(data);
    want_response_body.add(data);
    test_time_->advanceTimeWait(std::chrono::microseconds(10));
    stream_callbacks_->onReceiveMessage(std::move(response));
  }

  void processRequestTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true) {
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

  void processResponseTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true) {
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

  void streamingSmallChunksWithBodyMutation(bool empty_last_chunk, bool mutate_last_chunk) {
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
  void expectFilterState(const Envoy::Protobuf::Struct& expected_metadata) {
    const auto* filterState =
        stream_info_.filterState()
            ->getDataReadOnly<
                Envoy::Extensions::HttpFilters::ExternalProcessing::ExtProcLoggingInfo>(
                filter_config_name);
    const Envoy::Protobuf::Struct& loggedMetadata = filterState->filterMetadata();
    EXPECT_THAT(loggedMetadata, ProtoEq(expected_metadata));
  }

  absl::optional<envoy::config::core::v3::GrpcService> final_expected_grpc_service_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  std::unique_ptr<MockClient> client_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  ProcessingRequest last_request_;
  bool server_closed_stream_ = false;
  bool observability_mode_ = false;
  testing::NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
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
  Event::MockTimer* deferred_close_timer_;
  Envoy::Event::SimulatedTimeSystem* test_time_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  testing::NiceMock<Network::MockConnection> connection_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder_;
  TestScopedRuntime scoped_runtime_;
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
  EXPECT_TRUE(last_request_.has_protocol_config());
  EXPECT_EQ(last_request_.protocol_config().request_body_mode(), ProcessingMode::NONE);
  EXPECT_EQ(last_request_.protocol_config().response_body_mode(), ProcessingMode::NONE);
  EXPECT_FALSE(last_request_.protocol_config().send_body_without_waiting_for_header_response());
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
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processResponseHeaders(
      false, [](const HttpHeaders& header_resp, ProcessingResponse&, HeadersResponse&) {
        EXPECT_FALSE(header_resp.end_of_stream());
        TestRequestHeaderMapImpl expected_response{
            {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "3"}};
        EXPECT_THAT(header_resp.headers(), HeaderProtosEqual(expected_response));
      });

  EXPECT_EQ(1, config_->stats().streams_closed_.value());
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

  Envoy::Protobuf::Struct filter_metadata;
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
        add1->mutable_header()->set_raw_value("new");
        add1->mutable_append()->set_value(false);
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header");
        add2->mutable_header()->set_raw_value("no");
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
    resp_add1->mutable_append()->set_value(false);
    resp_add1->mutable_header()->set_key("x-new-header");
    resp_add1->mutable_header()->set_raw_value("new");
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

TEST_F(HttpFilterTest, ProcessingRequestModifier) {
  TestProcessingRequestModifierFactory factory;
  Registry::InjectFactory<ProcessingRequestModifierFactory> registration(factory);

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_request_modifier:
    name: "test_processing_request_modifier"
    typed_config:
      "@type": "type.googleapis.com/google.protobuf.Struct"
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Check that our custom attribute builder was used
  processRequestHeaders(false,
                        [](const HttpHeaders& header_req, ProcessingResponse&, HeadersResponse&) {
                          EXPECT_FALSE(header_req.end_of_stream());
                          TestRequestHeaderMapImpl expected{{":path", "/"},
                                                            {":method", "POST"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"},
                                                            {"x-test-request-modifier", ""}};
                          EXPECT_THAT(header_req.headers(), HeaderProtosEqual(expected));
                        });

  // Let the rest of the request play out
  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, ProcessingRequestModifierOverrides) {
  TestProcessingRequestModifierFactory factory;
  Registry::InjectFactory<ProcessingRequestModifierFactory> registration(factory);

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  ExtProcPerRoute route_proto;
  Envoy::Protobuf::Struct empty;
  auto* modifier_config = route_proto.mutable_overrides()->mutable_processing_request_modifier();
  modifier_config->set_name("test_processing_request_modifier");
  modifier_config->mutable_typed_config()->PackFrom(empty);

  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  // Check that our custom attribute builder was used
  processRequestHeaders(false,
                        [](const HttpHeaders& header_req, ProcessingResponse&, HeadersResponse&) {
                          EXPECT_FALSE(header_req.end_of_stream());
                          TestRequestHeaderMapImpl expected{{":path", "/"},
                                                            {":method", "POST"},
                                                            {":scheme", "http"},
                                                            {":authority", "host"},
                                                            {"x-test-request-modifier", ""}};
                          EXPECT_THAT(header_req.headers(), HeaderProtosEqual(expected));
                        });

  // Let the rest of the request play out
  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();
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
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
  auto* hdr2 = immediate_headers->add_set_headers();
  hdr2->mutable_append()->set_value(true);
  hdr2->mutable_header()->set_key("x-another-thing");
  hdr2->mutable_header()->set_raw_value("1");
  auto* hdr3 = immediate_headers->add_set_headers();
  hdr3->mutable_append()->set_value(true);
  hdr3->mutable_header()->set_key("x-another-thing");
  hdr3->mutable_header()->set_raw_value("2");
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

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

  expectFilterState(Envoy::Protobuf::Struct());
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
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
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

  EXPECT_FALSE(last_request_.observability_mode());
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

TEST_F(HttpFilterTest, RespondImmediatelyWithBinaryBody) {
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

  EXPECT_FALSE(last_request_.observability_mode());
  ASSERT_TRUE(last_request_.has_response_headers());

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::BadRequest, "non-utf-8 compliant field\x80\x81",
                             _, Eq(absl::nullopt), "Got_a_bad_request"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  std::unique_ptr<ProcessingResponse> resp2 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp2->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::BadRequest);
  immediate_response->set_body("non-utf-8 compliant field\x80\x81");
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

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);

  // Pretend that Envoy ignores the watermark and keep sending. It often does!
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_2, false));
  buffered_data.add(req_data_2);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_3, false));
  buffered_data.add(req_data_3);
  // when end_stream is true, we still do buffering.
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_4, true));
  buffered_data.add(req_data_4);

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

  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(req_data_2, false));
  buffered_data.add(req_data_2);

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

  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_1, false));
  buffered_data.add(req_data_1);
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(req_data_2, true));
  buffered_data.add(req_data_2);

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
  streamingSmallChunksWithBodyMutation(true, true);
}

TEST_F(HttpFilterTest, StreamingBodyNotMutateLastEmptyChunk) {
  streamingSmallChunksWithBodyMutation(true, false);
}

TEST_F(HttpFilterTest, StreamingBodyMutateLastChunk) {
  streamingSmallChunksWithBodyMutation(false, true);
}

TEST_F(HttpFilterTest, StreamingBodyNotMutateLastChunk) {
  streamingSmallChunksWithBodyMutation(false, false);
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
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");

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
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");
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
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");

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
  EXPECT_TRUE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(50));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(80));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(60));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(30));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, false));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt, false, std::chrono::microseconds(100));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
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
  EXPECT_TRUE(last_request_.has_protocol_config());
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
  EXPECT_FALSE(last_request_.has_protocol_config());
  processRequestBody(absl::nullopt);
  EXPECT_EQ(want_request_body.toString(), got_request_body.toString());
  EXPECT_FALSE(decoding_watermarked);
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "100");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_CALL(encoder_callbacks_, encodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(0, config_->stats().streams_closed_.value());
  EXPECT_FALSE(last_request_.has_protocol_config());
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
    EXPECT_FALSE(last_request_.has_protocol_config());
    processResponseBody(absl::nullopt, false);
  }
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

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

  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, true))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));
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
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");

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
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));
  server_closed_stream_ = true;
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");

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
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error message");

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

  EXPECT_FALSE(last_request_.observability_mode());
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

  EXPECT_FALSE(last_request_.observability_mode());
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

// DEFAULT header overrides do not affect response processing.
TEST_F(HttpFilterTest, ProcessingModeOverrideResponseHeadersDefault) {
  // Setup processing_mode to send all headers (default) and trailers (SEND).
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  allow_mode_override: true
  processing_mode:
    request_trailer_mode: "SEND"
    response_trailer_mode: "SEND"
  )EOF");
  EXPECT_EQ(filter_->config().allowModeOverride(), true);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  // Override header processing with DEFAULT.
  processRequestHeaders(false,
                        [](const HttpHeaders&, ProcessingResponse& response, HeadersResponse&) {
                          auto mode = response.mutable_mode_override();
                          mode->set_request_body_mode(ProcessingMode::NONE);
                          mode->set_request_trailer_mode(ProcessingMode::DEFAULT);
                          mode->set_response_header_mode(ProcessingMode::DEFAULT);
                          mode->set_response_body_mode(ProcessingMode::NONE);
                          mode->set_response_trailer_mode(ProcessingMode::DEFAULT);
                        });
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, [](const HttpHeaders& header_resp, ProcessingResponse&,
                                   HeadersResponse&) {
    EXPECT_FALSE(header_resp.end_of_stream());
    TestRequestHeaderMapImpl expected_response{{":status", "200"}, {"content-type", "text/plain"}};
    EXPECT_THAT(header_resp.headers(), HeaderProtosEqual(expected_response));
  });

  TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                   {"content-type", "text/plain"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(4, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
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

// Set allow_mode_override in filter config to be true.
// Set send_body_without_waiting_for_header_response to be true
// In such case, the mode_override in the response will be ignored.
TEST_F(HttpFilterTest, DisableResponseModeOverrideBySendBodyFlag) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
  allow_mode_override: true
  send_body_without_waiting_for_header_response: true
  )EOF");

  EXPECT_EQ(filter_->config().allowModeOverride(), true);
  EXPECT_EQ(filter_->config().sendBodyWithoutWaitingForHeaderResponse(), true);
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
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillOnce(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));
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

TEST_F(HttpFilterTest, ProtocolConfigEncodingPerRouteTest) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SKIP"
  )EOF");

  // Route configuration overrides the processing mode.
  ExtProcPerRoute route_proto;
  auto* processing_mode = route_proto.mutable_overrides()->mutable_processing_mode();
  processing_mode->set_request_body_mode(ProcessingMode::STREAMED);
  processing_mode->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillOnce(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  EXPECT_TRUE(last_request_.has_protocol_config());
  EXPECT_EQ(last_request_.protocol_config().request_body_mode(), ProcessingMode::STREAMED);
  EXPECT_EQ(last_request_.protocol_config().response_body_mode(),
            ProcessingMode::FULL_DUPLEX_STREAMED);
  filter_->onDestroy();
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
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
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
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_upstream_ignored_.value(), 0);
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
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
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
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
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
  EXPECT_EQ(config_->stats().clear_route_cache_upstream_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 3);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 3);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

// Using the default configuration, "clear_route_cache" flag not set. No header mutation.
TEST_F(HttpFilterTest, ClearRouteCacheUnchangedNoClearFlag) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF");

  // Do not call ClearRouteCache() for inbound traffic without header mutation.
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

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

  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_upstream_ignored_.value(), 0);
}

// Verify that with header mutation in response, setting route_cache_action to CLEAR
// will clear route cache even the response does not set clear_route_cache.
TEST_F(HttpFilterTest, FilterRouteCacheActionSetToClearHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  route_cache_action: CLEAR
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_upstream_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 2);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 2);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

// Verify that without header mutation in response, setting route_cache_action to CLEAR
// and set the clear_route_cache flag to true in the response will not clear route cache.
TEST_F(HttpFilterTest, FilterRouteCacheActionSetToClearNoHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  route_cache_action: CLEAR
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 1);
}

// Verify that without header mutation in response, setting route_cache_action to CLEAR and not
// set the clear_route_cache flag to true in the response will not clear route cache.
TEST_F(HttpFilterTest, FilterRouteCacheActionSetToClearResponseNotSetNoHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  route_cache_action: CLEAR
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  // There is no clear_route_cache set in the response. clear_route_cache_ignored_ is zero in this
  // case.
  processRequestHeaders(false, absl::nullopt);

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
}

// Verify that setting route_cache_action to RETAIN will not clear route cache.
TEST_F(HttpFilterTest, FilterRouteCacheActionSetToRetainWithHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  route_cache_action: RETAIN
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  filter_->onDestroy();

  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 1);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
}

// Verify that setting route_cache_action to RETAIN, response clear_route_cache flag not set,
// will not clear route cache.
TEST_F(HttpFilterTest, FilterRouteCacheActionSetToRetainResponseNotWithHeaderMutation) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  route_cache_action: RETAIN
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
  });

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);

  filter_->onDestroy();

  // This counter will not increase as clear_route_cache flag in the response is not set.
  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
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
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key(":method");
        hdr->mutable_header()->set_raw_value("POST");
        hdrs_resp.mutable_response()->mutable_body_mutation()->set_body("Hello, World!");
      });

  TestRequestHeaderMapImpl expected_request{
      {":scheme", "http"}, {":authority", "host"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_THAT(&request_headers_, HeaderMapEqualIgnoreOrder(&expected_request));
  EXPECT_EQ(req_buffer.toString(), "Hello, World!");
  EXPECT_EQ(0, config_->stats().streams_closed_.value());

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);
  EXPECT_EQ(response_headers_.getContentLengthValue(), "200");
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

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
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key("x-test-header");
        hdr->mutable_header()->set_raw_value("true");
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

// With failure_mode_allow set to true, tests the filter with a processor that
// replies to the request_headers message incorrectly by sending a
// request_body message, which should result in the stream being closed
// and ignored.
TEST_F(HttpFilterTest, OutOfOrderFailOpen) {
  initialize(R"EOF(
  failure_mode_allow: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.observability_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

  // Return an out-of-order message. The server should close the stream
  // and continue as if nothing happened.
  // failure_mode_allowed_ stats counter is incremented by 1.
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
  EXPECT_EQ(1, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// With failure_mode_allow set to false, i.e, default case, tests the filter with
// a processor that replies to the request_headers message incorrectly by sending
// a request_body message, which should result in local reply being sent.
TEST_F(HttpFilterTest, OutOfOrderFailClose) {
  initialize(R"EOF(
  failure_mode_allow: false
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_FALSE(last_request_.observability_mode());
  ASSERT_TRUE(last_request_.has_request_headers());

  // Return an out-of-order message. Spurious message stats counter is
  // incremented by 1. Failure mode stats counter is not incremented.
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_body();
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(0, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

class OverrideTest : public testing::Test {
protected:
  void SetUp() override {
    auto builder_ptr = Envoy::Extensions::Filters::Common::Expr::createBuilder({});
    builder_ = std::make_shared<Envoy::Extensions::Filters::Common::Expr::BuilderInstance>(
        std::move(builder_ptr));
  }

  Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

TEST_F(HttpFilterTest, OutOfOrderPerRouteOverrideFailOpen) {
  // Filter is configured with fail-close.
  initialize(R"EOF(
  failure_mode_allow: false
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");
  // Per-route overrides config to fail-open.
  ExtProcPerRoute route_proto;
  route_proto.mutable_overrides()->mutable_failure_mode_allow()->set_value(true);
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  ASSERT_TRUE(last_request_.has_request_headers());

  // Return an out-of-order message. Spurious message stats counter
  // and failure_mode_allow stats counter are both incremented by 1.
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  resp1->mutable_request_body();
  stream_callbacks_->onReceiveMessage(std::move(resp1));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().spurious_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

// When merging two configurations, ensure that the second processing mode
// overrides the first.
TEST_F(OverrideTest, OverrideProcessingMode) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_processing_mode()->set_request_body_mode(
      ProcessingMode::STREAMED);
  cfg2.mutable_overrides()->mutable_processing_mode()->set_response_body_mode(
      ProcessingMode::BUFFERED);
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  EXPECT_FALSE(merged_route.disabled());
  EXPECT_EQ(merged_route.processingMode()->request_header_mode(), ProcessingMode::DEFAULT);
  EXPECT_EQ(merged_route.processingMode()->request_body_mode(), ProcessingMode::STREAMED);
  EXPECT_EQ(merged_route.processingMode()->response_body_mode(), ProcessingMode::BUFFERED);
}

// When merging two configurations, if the first processing mode is set, and
// the second is disabled, then the filter should be disabled.
TEST_F(OverrideTest, DisableOverridesFirstMode) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  ExtProcPerRoute cfg2;
  cfg2.set_disabled(true);
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  EXPECT_TRUE(merged_route.disabled());
  EXPECT_FALSE(merged_route.processingMode());
}

// When merging two configurations, if the first override is disabled, and
// the second has a new mode, then the filter should use the new mode.
TEST_F(OverrideTest, ModeOverridesFirstDisable) {
  ExtProcPerRoute cfg1;
  cfg1.set_disabled(true);
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_processing_mode()->set_request_header_mode(
      ProcessingMode::SKIP);
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  EXPECT_FALSE(merged_route.disabled());
  EXPECT_EQ(merged_route.processingMode()->request_header_mode(), ProcessingMode::SKIP);
}

// When merging two configurations, if both are disabled, then it's still
// disabled.
TEST_F(OverrideTest, DisabledThingsAreDisabled) {
  ExtProcPerRoute cfg1;
  cfg1.set_disabled(true);
  ExtProcPerRoute cfg2;
  cfg2.set_disabled(true);
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  EXPECT_TRUE(merged_route.disabled());
  EXPECT_FALSE(merged_route.processingMode());
}

// When merging two configurations, second grpc_service overrides the first.
TEST_F(OverrideTest, GrpcServiceOverride) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");
  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_2");
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  ASSERT_TRUE(merged_route.grpcService().has_value());
  EXPECT_THAT(*merged_route.grpcService(), ProtoEq(cfg2.overrides().grpc_service()));
}

// When merging two configurations, unset grpc_service is equivalent to no override.
TEST_F(OverrideTest, GrpcServiceNonOverride) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
      "cluster_1");
  ExtProcPerRoute cfg2;
  // Leave cfg2.grpc_service unset.
  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);
  ASSERT_TRUE(merged_route.grpcService().has_value());
  EXPECT_THAT(*merged_route.grpcService(), ProtoEq(cfg1.overrides().grpc_service()));
}

// When merging two configurations, second metadata override only extends the first's one.
TEST_F(OverrideTest, GrpcMetadataOverride) {
  ExtProcPerRoute cfg1;
  cfg1.mutable_overrides()->mutable_grpc_initial_metadata()->Add()->CopyFrom(
      makeHeaderValue("a", "a"));
  cfg1.mutable_overrides()->mutable_grpc_initial_metadata()->Add()->CopyFrom(
      makeHeaderValue("b", "b"));

  ExtProcPerRoute cfg2;
  cfg2.mutable_overrides()->mutable_grpc_initial_metadata()->Add()->CopyFrom(
      makeHeaderValue("b", "c"));
  cfg2.mutable_overrides()->mutable_grpc_initial_metadata()->Add()->CopyFrom(
      makeHeaderValue("c", "c"));

  FilterConfigPerRoute route1(cfg1, builder_, factory_context_);
  FilterConfigPerRoute route2(cfg2, builder_, factory_context_);
  FilterConfigPerRoute merged_route(route1, route2);

  ASSERT_TRUE(merged_route.grpcInitialMetadata().size() == 3);
  EXPECT_THAT(merged_route.grpcInitialMetadata()[0],
              ProtoEq(cfg1.overrides().grpc_initial_metadata()[0]));
  EXPECT_THAT(merged_route.grpcInitialMetadata()[1],
              ProtoEq(cfg2.overrides().grpc_initial_metadata()[0]));
  EXPECT_THAT(merged_route.grpcInitialMetadata()[2],
              ProtoEq(cfg2.overrides().grpc_initial_metadata()[1]));
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
        add1->mutable_append()->set_value(false);
        add1->mutable_header()->set_key("Host");
        add1->mutable_header()->set_raw_value("wrong:1234");
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
  add1->mutable_append()->set_value(false);
  add1->mutable_header()->set_key("Host");
  add1->mutable_header()->set_raw_value("wrong:1234");
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
  EXPECT_TRUE(last_request_.has_protocol_config());
  processResponseHeaders(false, absl::nullopt);
  // Construct a large trailer message to be close to the HCM size limit.
  resp_trailers_.addCopy(LowerCaseString("x-some-trailer"), std::string(1950, 'a'));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(resp_trailers_));
  EXPECT_FALSE(last_request_.has_protocol_config());
  processResponseTrailers(
      [](const HttpTrailers&, ProcessingResponse&, TrailersResponse& trailer_resp) {
        auto headers_mut = trailer_resp.mutable_header_mutation();
        // The trailer mutation in the response does not exceed the count limit 100 or the
        // size limit 2kb. But the result header map size exceeds the count limit 2kb.
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_append()->set_value(false);
        add1->mutable_header()->set_key("x-new-header-0123456789");
        add1->mutable_header()->set_raw_value("new-header-0123456789");
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_append()->set_value(false);
        add2->mutable_header()->set_key("x-some-other-header-0123456789");
        add2->mutable_header()->set_raw_value("some-new-header-0123456789");
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

TEST_F(HttpFilterTest, MetadataOptionsOverride) {
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
  metadata_options:
    forwarding_namespaces:
      untyped:
      - untyped_ns_1
      typed:
      - typed_ns_1
    receiving_namespaces:
      untyped:
      - untyped_receiving_ns_1
  )EOF");
  ExtProcPerRoute override_cfg;
  const std::string override_yaml = R"EOF(
  overrides:
    metadata_options:
      forwarding_namespaces:
        untyped:
        - untyped_ns_2
        typed:
        - typed_ns_2
      receiving_namespaces:
        untyped:
        - untyped_receiving_ns_2
  )EOF";
  TestUtility::loadFromYaml(override_yaml, override_cfg);

  FilterConfigPerRoute route_config(override_cfg, builder_, factory_context_);

  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillOnce(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  ASSERT_EQ(filter_->encodingState().untypedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->encodingState().untypedForwardingMetadataNamespaces()[0], "untyped_ns_2");
  ASSERT_EQ(filter_->decodingState().untypedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().untypedForwardingMetadataNamespaces()[0], "untyped_ns_2");

  ASSERT_EQ(filter_->encodingState().typedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().typedForwardingMetadataNamespaces()[0], "typed_ns_2");

  ASSERT_EQ(filter_->encodingState().untypedReceivingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->encodingState().untypedReceivingMetadataNamespaces()[0],
            "untyped_receiving_ns_2");
  ASSERT_EQ(filter_->decodingState().untypedReceivingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().untypedReceivingMetadataNamespaces()[0],
            "untyped_receiving_ns_2");

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  filter_->onDestroy();
}

// Validate that when metadata options are not specified as an override, the less-specific
// namespaces lists are used.
TEST_F(HttpFilterTest, MetadataOptionsNoOverride) {
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
  metadata_options:
    forwarding_namespaces:
      untyped:
      - untyped_ns_1
      typed:
      - typed_ns_1
    receiving_namespaces:
      untyped:
      - untyped_receiving_ns_1
  )EOF");
  ExtProcPerRoute override_cfg;
  const std::string override_yaml = R"EOF(
  overrides: {}
  )EOF";
  TestUtility::loadFromYaml(override_yaml, override_cfg);

  FilterConfigPerRoute route_config(override_cfg, builder_, factory_context_);

  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillOnce(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  response_headers_.addCopy(LowerCaseString("content-length"), "3");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(false, absl::nullopt);

  ASSERT_EQ(filter_->encodingState().untypedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->encodingState().untypedForwardingMetadataNamespaces()[0], "untyped_ns_1");
  ASSERT_EQ(filter_->decodingState().untypedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().untypedForwardingMetadataNamespaces()[0], "untyped_ns_1");

  ASSERT_EQ(filter_->encodingState().typedForwardingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().typedForwardingMetadataNamespaces()[0], "typed_ns_1");

  ASSERT_EQ(filter_->encodingState().untypedReceivingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->encodingState().untypedReceivingMetadataNamespaces()[0],
            "untyped_receiving_ns_1");
  ASSERT_EQ(filter_->decodingState().untypedReceivingMetadataNamespaces().size(), 1);
  EXPECT_EQ(filter_->decodingState().untypedReceivingMetadataNamespaces()[0],
            "untyped_receiving_ns_1");

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  filter_->onDestroy();
}

// Verify that the filter sets the processing request with dynamic metadata
// including when the metadata is on the connection stream info
TEST_F(HttpFilterTest, SendDynamicMetadata) {
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
  metadata_options:
    forwarding_namespaces:
      untyped:
      - connection.and.request.have.data
      - connection.has.data
      - request.has.data
      - neither.have.data
      - untyped.and.typed.connection.data
      - typed.connection.data
      - untyped.connection.data
      typed:
      - untyped.and.typed.connection.data
      - typed.connection.data
      - typed.request.data
      - untyped.connection.data
  )EOF");

  const std::string request_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: request
    request.has.data:
      data: request
  typed_filter_metadata:
    typed.request.data:
      # We are using ExtProcOverrides just because we know it is built and imported already.
      '@type': type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExtProcOverrides
      request_attributes:
      - request_typed
  )EOF";

  const std::string connection_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: connection_untyped
    connection.has.data:
      data: connection_untyped
    untyped.and.typed.connection.data:
      data: connection_untyped
    untyped.connection.data:
      data: connection_untyped
    not.selected.data:
      data: connection_untyped
  typed_filter_metadata:
    untyped.and.typed.connection.data:
      # We are using ExtProcOverrides just because we know it is built and imported already.
      '@type': type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExtProcOverrides
      request_attributes:
      - connection_typed
    typed.connection.data:
      # We are using ExtProcOverrides just because we know it is built and imported already.
      '@type': type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExtProcOverrides
      request_attributes:
      - connection_typed
    not.selected.data:
      # We are using ExtProcOverrides just because we know it is built and imported already.
      '@type': type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExtProcOverrides
      request_attributes:
      - connection_typed
  )EOF";

  envoy::config::core::v3::Metadata connection_metadata;
  TestUtility::loadFromYaml(request_yaml, dynamic_metadata_);
  TestUtility::loadFromYaml(connection_yaml, connection_metadata);
  connection_.stream_info_.metadata_ = connection_metadata;

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  // ensure the metadata that is attached to the processing request is identical to
  // the metadata we specified above
  EXPECT_EQ("request", last_request_.metadata_context()
                           .filter_metadata()
                           .at("connection.and.request.have.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("request", last_request_.metadata_context()
                           .filter_metadata()
                           .at("request.has.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("connection_untyped", last_request_.metadata_context()
                                      .filter_metadata()
                                      .at("connection.has.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ("connection_untyped", last_request_.metadata_context()
                                      .filter_metadata()
                                      .at("untyped.and.typed.connection.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ(0, last_request_.metadata_context().filter_metadata().count("neither.have.data"));

  EXPECT_EQ(0, last_request_.metadata_context().filter_metadata().count("not.selected.data"));

  EXPECT_EQ(0, last_request_.metadata_context().filter_metadata().count("typed.connection.data"));

  envoy::extensions::filters::http::ext_proc::v3::ExtProcOverrides typed_any;
  last_request_.metadata_context()
      .typed_filter_metadata()
      .at("typed.connection.data")
      .UnpackTo(&typed_any);
  ASSERT_EQ(1, typed_any.request_attributes().size());
  EXPECT_EQ("connection_typed", typed_any.request_attributes()[0]);

  last_request_.metadata_context()
      .typed_filter_metadata()
      .at("untyped.and.typed.connection.data")
      .UnpackTo(&typed_any);
  ASSERT_EQ(1, typed_any.request_attributes().size());
  EXPECT_EQ("connection_typed", typed_any.request_attributes()[0]);

  EXPECT_EQ(
      0, last_request_.metadata_context().typed_filter_metadata().count("untyped.connection.data"));

  EXPECT_EQ(0, last_request_.metadata_context().typed_filter_metadata().count("not.selected.data"));

  processResponseHeaders(false, absl::nullopt);

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  filter_->onDestroy();
}

// Verify that when returning an response with dynamic_metadata field set, the filter emits
// dynamic metadata.
TEST_F(HttpFilterTest, EmitDynamicMetadata) {
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
  metadata_options:
    receiving_namespaces:
      untyped:
      - envoy.filters.http.ext_proc
  )EOF");

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct foobar;
    (*foobar.mutable_fields())["foo"].set_string_value("bar");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_proc"].mutable_struct_value();
    *mut_struct = foobar;
  });

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  EXPECT_EQ("bar", dynamic_metadata_.filter_metadata()
                       .at("envoy.filters.http.ext_proc")
                       .fields()
                       .at("foo")
                       .string_value());

  filter_->onDestroy();
}

// Verify that when returning an response with dynamic_metadata field set, the filter emits
// dynamic metadata to namespaces other than its own.
TEST_F(HttpFilterTest, EmitDynamicMetadataArbitraryNamespace) {
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
  metadata_options:
    receiving_namespaces:
      untyped:
      - envoy.filters.http.ext_authz
  )EOF");

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct foobar;
    (*foobar.mutable_fields())["foo"].set_string_value("bar");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_authz"].mutable_struct_value();
    *mut_struct = foobar;
  });

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  EXPECT_EQ("bar", dynamic_metadata_.filter_metadata()
                       .at("envoy.filters.http.ext_authz")
                       .fields()
                       .at("foo")
                       .string_value());

  filter_->onDestroy();
}

// Verify that when returning an response with dynamic_metadata field set, the
// filter does not emit metadata when no allowed namespaces are configured.
TEST_F(HttpFilterTest, DisableEmitDynamicMetadata) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  )EOF");

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct foobar;
    (*foobar.mutable_fields())["foo"].set_string_value("bar");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_proc"].mutable_struct_value();
    *mut_struct = foobar;
  });

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  EXPECT_EQ(0, dynamic_metadata_.filter_metadata().size());

  filter_->onDestroy();
}

// Verify that when returning an response with dynamic_metadata field set, the
// filter does not emit metadata to namespaces which are not allowed.
TEST_F(HttpFilterTest, DisableEmittingDynamicMetadataToDisallowedNamespaces) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  metadata_options:
    receiving_namespaces:
      untyped:
      - envoy.filters.http.ext_proc
  )EOF");

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct foobar;
    (*foobar.mutable_fields())["foo"].set_string_value("bar");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_authz"].mutable_struct_value();
    *mut_struct = foobar;
  });

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  EXPECT_EQ(0, dynamic_metadata_.filter_metadata().size());

  filter_->onDestroy();
}

// Verify that when returning an response with dynamic_metadata field set, the filter emits
// dynamic metadata and later emissions overwrite earlier ones.
TEST_F(HttpFilterTest, EmitDynamicMetadataUseLast) {
  // Configure the filter to only pass response headers to ext server.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SEND"
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SKIP"
  metadata_options:
    receiving_namespaces:
      untyped:
      - envoy.filters.http.ext_proc
  )EOF");

  Buffer::OwnedImpl empty_chunk;

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct batbaz;
    (*batbaz.mutable_fields())["bat"].set_string_value("baz");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_proc"].mutable_struct_value();
    *mut_struct = batbaz;
  });
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    Protobuf::Struct foobar;
    (*foobar.mutable_fields())["foo"].set_string_value("bar");
    auto metadata_mut = resp.mutable_dynamic_metadata()->mutable_fields();
    auto mut_struct = (*metadata_mut)["envoy.filters.http.ext_proc"].mutable_struct_value();
    *mut_struct = foobar;
  });

  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_chunk, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));

  EXPECT_FALSE(dynamic_metadata_.filter_metadata()
                   .at("envoy.filters.http.ext_proc")
                   .fields()
                   .contains("bat"));

  EXPECT_EQ("bar", dynamic_metadata_.filter_metadata()
                       .at("envoy.filters.http.ext_proc")
                       .fields()
                       .at("foo")
                       .string_value());

  filter_->onDestroy();
}

TEST_F(HttpFilterTest, HeaderRespReceivedBeforeBody) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SEND"
    response_body_mode: "STREAMED"
  send_body_without_waiting_for_header_response: true
  )EOF");

  EXPECT_EQ(config_->sendBodyWithoutWaitingForHeaderResponse(), true);

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  // Header response arrives before any body data.
  processResponseHeaders(false, absl::nullopt);

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }

  // Send body responses
  for (int i = 0; i < 5; i++) {
    processResponseBody(
        [&want_response_body, i](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
          auto* body_mut = resp.mutable_response()->mutable_body_mutation();
          std::string new_body = absl::StrCat(" ", std::to_string(i), " ");
          body_mut->set_body(new_body);
          want_response_body.add(new_body);
        },
        false);
  }

  // Send the last empty request chunk.
  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);
  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, HeaderRespReceivedAfterBodySent) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SEND"
    response_body_mode: "STREAMED"
  send_body_without_waiting_for_header_response: true
  )EOF");

  EXPECT_EQ(config_->sendBodyWithoutWaitingForHeaderResponse(), true);

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  EXPECT_TRUE(last_request_.has_protocol_config());
  EXPECT_EQ(last_request_.protocol_config().request_body_mode(), ProcessingMode::NONE);
  EXPECT_EQ(last_request_.protocol_config().response_body_mode(), ProcessingMode::STREAMED);
  EXPECT_TRUE(last_request_.protocol_config().send_body_without_waiting_for_header_response());

  Buffer::OwnedImpl want_response_body;
  Buffer::OwnedImpl got_response_body;
  EXPECT_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&got_response_body](Buffer::Instance& data, Unused) { got_response_body.move(data); }));

  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, false));
  }

  // Header response arrives after some amount of body data sent.
  auto response = std::make_unique<ProcessingResponse>();
  (void)response->mutable_response_headers();
  stream_callbacks_->onReceiveMessage(std::move(response));

  // Three body responses follows the header response.
  for (int i = 0; i < 2; i++) {
    processResponseBody(
        [&want_response_body, i](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
          auto* body_mut = resp.mutable_response()->mutable_body_mutation();
          std::string new_body = absl::StrCat(" ", std::to_string(i), " ");
          body_mut->set_body(new_body);
          want_response_body.add(new_body);
        },
        false);
  }

  // Now sends the rest of the body chunks to the server.
  for (int i = 5; i < 10; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_chunk, false));
  }

  // Send body responses
  for (int i = 2; i < 10; i++) {
    processResponseBody(
        [&want_response_body, i](const HttpBody&, ProcessingResponse&, BodyResponse& resp) {
          auto* body_mut = resp.mutable_response()->mutable_body_mutation();
          std::string new_body = absl::StrCat(" ", std::to_string(i), " ");
          body_mut->set_body(new_body);
          want_response_body.add(new_body);
        },
        false);
  }

  // Send the last empty request chunk.
  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);

  // The two buffers should match.
  EXPECT_EQ(want_response_body.toString(), got_response_body.toString());
  EXPECT_FALSE(encoding_watermarked);
  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, HeaderRespWithStatusContinueAndReplace) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SKIP"
    response_header_mode: "SEND"
    response_body_mode: "STREAMED"
  send_body_without_waiting_for_header_response: true
  )EOF");

  EXPECT_EQ(config_->sendBodyWithoutWaitingForHeaderResponse(), true);

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, false));
  }

  Buffer::OwnedImpl resp_buffer;
  setUpEncodingBuffering(resp_buffer, true);
  // Header response arrives with status CONTINUE_AND_REPLACE after some amount of body data sent.
  auto response = std::make_unique<ProcessingResponse>();
  auto* hdrs_resp = response->mutable_response_headers();
  hdrs_resp->mutable_response()->set_status(CommonResponse::CONTINUE_AND_REPLACE);
  hdrs_resp->mutable_response()->mutable_body_mutation()->set_body("Hello, World!");
  stream_callbacks_->onReceiveMessage(std::move(response));

  // Ensure buffered data was updated
  EXPECT_EQ(resp_buffer.toString(), "Hello, World!");

  // Since we did CONTINUE_AND_REPLACE, later data is cleared
  Buffer::OwnedImpl resp_data_1("test");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data_1, false));
  EXPECT_EQ(resp_data_1.length(), 0);

  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, StreamedTestInBothDirection) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: "SEND"
    request_body_mode: "STREAMED"
    response_header_mode: "SEND"
    response_body_mode: "STREAMED"
  send_body_without_waiting_for_header_response: true
  )EOF");

  EXPECT_EQ(config_->sendBodyWithoutWaitingForHeaderResponse(), true);

  // Create synthetic HTTP request
  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  request_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  for (int i = 0; i < 5; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(resp_chunk, false));
  }
  // Send the last empty request chunk.
  Buffer::OwnedImpl last_req_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(last_req_chunk, true));
  // Header response arrives
  auto req_response = std::make_unique<ProcessingResponse>();
  (void)req_response->mutable_request_headers();
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  stream_callbacks_->onReceiveMessage(std::move(req_response));

  // Data response arrives
  for (int i = 0; i < 5; i++) {
    processRequestBody(absl::nullopt, false);
  }
  processRequestBody(absl::nullopt, false);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-type"), "text/plain");

  bool encoding_watermarked = false;
  setUpEncodingWatermarking(encoding_watermarked);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  for (int i = 0; i < 7; i++) {
    Buffer::OwnedImpl resp_chunk;
    TestUtility::feedBufferWithRandomCharacters(resp_chunk, 100);
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(resp_chunk, false));
  }

  auto resp_response = std::make_unique<ProcessingResponse>();
  (void)resp_response->mutable_response_headers();
  stream_callbacks_->onReceiveMessage(std::move(resp_response));

  // Send body responses
  for (int i = 0; i < 7; i++) {
    processResponseBody(absl::nullopt, false);
  }

  // Send the last empty request chunk.
  Buffer::OwnedImpl last_resp_chunk;
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(last_resp_chunk, true));
  processResponseBody(absl::nullopt, true);

  EXPECT_EQ(config_->stats().spurious_msgs_received_.value(), 0);
  filter_->onDestroy();
}

// Verify if ext_proc filter is in the upstream filter chain, and if the ext_proc server
// sends back response with clear_route_cache set to true, it is ignored.
TEST_F(HttpFilterTest, ClearRouteCacheHeaderMutationUpstreamIgnored) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_header_mode: "SKIP"
  )EOF",
             true);

  // When ext_proc filter is in upstream, clear_route_cache response is ignored.
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& resp) {
    auto* resp_headers_mut = resp.mutable_response()->mutable_header_mutation();
    auto* resp_add = resp_headers_mut->add_set_headers();
    resp_add->mutable_append()->set_value(false);
    resp_add->mutable_header()->set_key("x-new-header");
    resp_add->mutable_header()->set_raw_value("new");
    resp.mutable_response()->set_clear_route_cache(true);
  });

  filter_->onDestroy();

  // The clear_router_cache from response is ignored.
  EXPECT_EQ(config_->stats().clear_route_cache_upstream_ignored_.value(), 1);
  EXPECT_EQ(config_->stats().clear_route_cache_disabled_.value(), 0);
  EXPECT_EQ(config_->stats().clear_route_cache_ignored_.value(), 0);
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 1);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

TEST_F(HttpFilterTest, PostAndRespondImmediatelyUpstream) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF",
             true);

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
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
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
  auto* hdr2 = immediate_headers->add_set_headers();
  hdr2->mutable_append()->set_value(true);
  hdr2->mutable_header()->set_key("foo");
  hdr2->mutable_header()->set_raw_value("bar");
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  TestResponseHeaderMapImpl expected_response_headers{{"content-type", "text/plain"},
                                                      {"foo", "bar"}};
  EXPECT_THAT(&immediate_response_headers, HeaderMapEqualIgnoreOrder(&expected_response_headers));
  EXPECT_EQ(config_->stats().streams_started_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_sent_.value(), 1);
  EXPECT_EQ(config_->stats().stream_msgs_received_.value(), 1);
  EXPECT_EQ(config_->stats().streams_closed_.value(), 1);
}

// Test that per route metadata override does override inherited grpc_service configuration.
TEST_F(HttpFilterTest, GrpcServiceMetadataOverride) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
    initial_metadata:
      - key: "a"
        value: "a"
      - key: "b"
        value: "b"
  )EOF");

  // Route configuration overrides the grpc_service metadata.
  ExtProcPerRoute route_proto;
  *route_proto.mutable_overrides()->mutable_grpc_initial_metadata()->Add() =
      makeHeaderValue("b", "c");
  *route_proto.mutable_overrides()->mutable_grpc_initial_metadata()->Add() =
      makeHeaderValue("c", "c");
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillOnce(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  // Build expected merged grpc_service configuration.
  {
    std::string expected_config = (R"EOF(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_proc_server"
        initial_metadata:
          - key: "a"
            value: "a"
          - key: "b"
            value: "c"
          - key: "c"
            value: "c"
    )EOF");
    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor expected_proto{};
    TestUtility::loadFromYaml(expected_config, expected_proto);
    final_expected_grpc_service_.emplace(expected_proto.grpc_service());
    config_with_hash_key_.setConfig(expected_proto.grpc_service());
  }

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  const auto& meta = filter_->grpcServiceConfig().initial_metadata();
  EXPECT_EQ(meta[0].value(), "a"); // a = a inherited
  EXPECT_EQ(meta[1].value(), "c"); // b = c overridden
  EXPECT_EQ(meta[2].value(), "c"); // c = c added

  filter_->onDestroy();
}

// Test header mutation errors during response processing
TEST_F(HttpFilterTest, ResponseHeaderMutationErrors) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_header_mode: "SEND"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  // Process response with invalid header mutation
  processResponseHeaders(false, [](const HttpHeaders&, ProcessingResponse& resp, HeadersResponse&) {
    auto* header_mut =
        resp.mutable_response_headers()->mutable_response()->mutable_header_mutation();
    auto* header = header_mut->add_set_headers();
    header->mutable_append()->set_value(false);
    header->mutable_header()->set_key(":scheme");
    header->mutable_header()->set_raw_value("invalid");
  });

  filter_->onDestroy();
}

// Test invalid content length handling during response processing
TEST_F(HttpFilterTest, InvalidResponseContentLength) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    response_header_mode: "SEND"
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  processRequestHeaders(false, absl::nullopt);

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  response_headers_.addCopy(LowerCaseString("content-length"), "not_a_number");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));

  processResponseHeaders(false, absl::nullopt);
  filter_->onDestroy();
}

TEST_F(HttpFilterTest, OnProcessingResponseHeaders) {
  TestOnProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  on_processing_response:
    name: "abc"
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");

  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");
  request_headers_.addCopy(LowerCaseString("x-do-we-want-this"), "no");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& header_resp) {
        auto headers_mut = header_resp.mutable_response()->mutable_header_mutation();
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_header()->set_key("x-new-header");
        add1->mutable_header()->set_raw_value("new");
        add1->mutable_append()->set_value(false);
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header");
        add2->mutable_header()->set_raw_value("no");
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

  ASSERT_TRUE(
      dynamic_metadata_.filter_metadata().contains("envoy-test-ext_proc-request_headers_response"));
  const auto& request_headers_struct_metadata =
      dynamic_metadata_.filter_metadata().at("envoy-test-ext_proc-request_headers_response");
  Protobuf::Struct expected_request_headers;
  TestUtility::loadFromJson(R"EOF(
{
  "x-do-we-want-this": "remove",
  "x-new-header": "new",
  "x-some-other-header": "no"
})EOF",
                            expected_request_headers);
  EXPECT_TRUE(
      TestUtility::protoEqual(request_headers_struct_metadata, expected_request_headers, true));

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
    resp_add1->mutable_append()->set_value(false);
    resp_add1->mutable_header()->set_key("x-new-header");
    resp_add1->mutable_header()->set_raw_value("new");
  });

  // We should now have changed the original header a bit
  TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                   {"content-type", "text/plain"},
                                                   {"content-length", "3"},
                                                   {"x-new-header", "new"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  ASSERT_TRUE(dynamic_metadata_.filter_metadata().contains(
      "envoy-test-ext_proc-response_headers_response"));
  const auto& response_headers_struct_metadata =
      dynamic_metadata_.filter_metadata().at("envoy-test-ext_proc-response_headers_response");
  Protobuf::Struct expected_response_headers;
  TestUtility::loadFromJson(R"EOF(
{
  "x-new-header": "new",
})EOF",
                            expected_response_headers);

  EXPECT_TRUE(
      TestUtility::protoEqual(response_headers_struct_metadata, expected_response_headers, true));

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

TEST_F(HttpFilterTest, SaveProcessingResponseHeaders) {
  SaveProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  on_processing_response:
    name: "abc"
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.http.ext_proc.response_processors.save_processing_response.v3.SaveProcessingResponse
      save_request_headers:
        save_response: true
      save_response_headers:
        save_response: true
  )EOF");

  request_headers_.addCopy(LowerCaseString("x-some-other-header"), "yes");
  request_headers_.addCopy(LowerCaseString("x-do-we-want-this"), "no");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(
      false, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse& header_resp) {
        auto headers_mut = header_resp.mutable_response()->mutable_header_mutation();
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_header()->set_key("x-new-header");
        add1->mutable_header()->set_raw_value("new");
        add1->mutable_append()->set_value(false);
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header");
        add2->mutable_header()->set_raw_value("no");
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
  auto filter_state = stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
      SaveProcessingResponseFilterState::kFilterStateName);
  ASSERT_TRUE(filter_state->response.has_value());

  envoy::service::ext_proc::v3::ProcessingResponse expected_response;
  TestUtility::loadFromJson(
      R"EOF(
  {
  "requestHeaders": {
    "response": {
      "headerMutation": {
        "setHeaders": [{
          "header": {
            "key": "x-new-header",
            "rawValue": "bmV3"
          },
          "append": false
        }, {
          "header": {
            "key": "x-some-other-header",
            "rawValue": "bm8="
          },
          "append": true
        }],
        "removeHeaders": ["x-do-we-want-this"]
      }
    }
  }
})EOF",
      expected_response);

  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_response));

  filter_state->response.reset();

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
    resp_add1->mutable_append()->set_value(false);
    resp_add1->mutable_header()->set_key("x-new-header");
    resp_add1->mutable_header()->set_raw_value("new");
  });

  // We should now have changed the original header a bit
  TestRequestHeaderMapImpl final_expected_response{{":status", "200"},
                                                   {"content-type", "text/plain"},
                                                   {"content-length", "3"},
                                                   {"x-new-header", "new"}};
  EXPECT_THAT(&response_headers_, HeaderMapEqualIgnoreOrder(&final_expected_response));

  ASSERT_TRUE(filter_state->response.has_value());

  envoy::service::ext_proc::v3::ProcessingResponse expected_response_headers;
  TestUtility::loadFromJson(
      R"EOF(
{
  "responseHeaders": {
    "response": {
      "headerMutation": {
        "setHeaders": [{
          "header": {
            "key": "x-new-header",
            "rawValue": "bmV3"
          },
          "append": false
        }]
      }
    }
  }
})EOF",
      expected_response_headers);

  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_response_headers));
  filter_state->response.reset();

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

TEST_F(HttpFilterTest, OnProcessingResponseBodies) {
  TestOnProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
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
  on_processing_response:
    name: "abc"
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
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

  ASSERT_TRUE(
      dynamic_metadata_.filter_metadata().contains("envoy-test-ext_proc-request_body_response"));
  const auto& request_body_struct_metadata =
      dynamic_metadata_.filter_metadata().at("envoy-test-ext_proc-request_body_response");
  Protobuf::Struct expected_request_body;
  TestUtility::loadFromJson(R"EOF(
{
  "clear_body": "1"
})EOF",
                            expected_request_body);

  EXPECT_TRUE(TestUtility::protoEqual(request_body_struct_metadata, expected_request_body, true));
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

  ASSERT_TRUE(
      dynamic_metadata_.filter_metadata().contains("envoy-test-ext_proc-response_body_response"));
  const auto& response_body_struct_metadata =
      dynamic_metadata_.filter_metadata().at("envoy-test-ext_proc-response_body_response");
  Protobuf::Struct expected_response_body;
  TestUtility::loadFromJson(R"EOF(
{
  "body": "Hello, World!"
})EOF",
                            expected_response_body);
  EXPECT_TRUE(TestUtility::protoEqual(response_body_struct_metadata, expected_response_body, true));

  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(2, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
}

TEST_F(HttpFilterTest, SaveImmediateResponse) {
  SaveProcessingResponseFactory factory;
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
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
  auto* hdr2 = immediate_headers->add_set_headers();
  hdr2->mutable_append()->set_value(true);
  hdr2->mutable_header()->set_key("x-another-thing");
  hdr2->mutable_header()->set_raw_value("1");
  auto* hdr3 = immediate_headers->add_set_headers();
  hdr3->mutable_append()->set_value(true);
  hdr3->mutable_header()->set_key("x-another-thing");
  hdr3->mutable_header()->set_raw_value("2");
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
  auto filter_state = stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
      SaveProcessingResponseFilterState::kFilterStateName);
  ASSERT_TRUE(filter_state->response.has_value());
  envoy::service::ext_proc::v3::ProcessingResponse expected_response;
  TestUtility::loadFromJson(
      R"EOF(
{
  "immediateResponse": {
    "status": {
      "code": "BadRequest"
    },
    "headers": {
      "setHeaders": [{
        "header": {
          "key": "content-type",
          "rawValue": "dGV4dC9wbGFpbg=="
        }
      }, {
        "header": {
          "key": "x-another-thing",
          "rawValue": "MQ=="
        },
        "append": true
      }, {
        "header": {
          "key": "x-another-thing",
          "rawValue": "Mg=="
        },
        "append": true
      }]
    },
    "body": "QmFkIHJlcXVlc3Q=",
    "details": "Got a bad request"
  }
})EOF",
      expected_response);

  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_response));

  filter_state->response.reset();

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND);
  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);

  expectFilterState(Envoy::Protobuf::Struct());
}

TEST_F(HttpFilterTest, DontSaveImmediateResponse) {
  SaveProcessingResponseFactory factory;
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
        save_response: false
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
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  TestResponseHeaderMapImpl expected_response_headers{{"content-type", "text/plain"}};
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
  EXPECT_EQ(stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
                SaveProcessingResponseFilterState::kFilterStateName),
            nullptr);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  checkGrpcCallHeaderOnlyStats(envoy::config::core::v3::TrafficDirection::INBOUND);
  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);

  expectFilterState(Envoy::Protobuf::Struct());
}

TEST_F(HttpFilterTest, DontSaveImmediateResponseOnError) {
  SaveProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  disable_immediate_response: true
  on_processing_response:
    name: "abc"
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.http.ext_proc.response_processors.save_processing_response.v3.SaveProcessingResponse
      save_immediate_response:
        save_response: true
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  test_time_->advanceTimeWait(std::chrono::microseconds(10));
  std::unique_ptr<ProcessingResponse> resp1 = std::make_unique<ProcessingResponse>();
  auto* immediate_response = resp1->mutable_immediate_response();
  immediate_response->mutable_status()->set_code(envoy::type::v3::StatusCode::BadRequest);
  immediate_response->set_body("Bad request");
  immediate_response->set_details("Got a bad request");
  auto* immediate_headers = immediate_response->mutable_headers();
  auto* hdr1 = immediate_headers->add_set_headers();
  hdr1->mutable_append()->set_value(false);
  hdr1->mutable_header()->set_key("content-type");
  hdr1->mutable_header()->set_raw_value("text/plain");
  stream_callbacks_->onReceiveMessage(std::move(resp1));

  TestResponseHeaderMapImpl expected_response_headers{{"content-type", "text/plain"}};

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(req_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
  Buffer::OwnedImpl resp_data("bar");
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(resp_data, false));
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(empty_data, true));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
                SaveProcessingResponseFilterState::kFilterStateName),
            nullptr);

  filter_->onDestroy();

  EXPECT_EQ(1, config_->stats().streams_started_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(0, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());

  expectNoGrpcCall(envoy::config::core::v3::TrafficDirection::OUTBOUND);

  expectFilterState(Envoy::Protobuf::Struct());
}

TEST_F(HttpFilterTest, SaveResponseTrailers) {
  SaveProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
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
  on_processing_response:
    name: "abc"
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.http.ext_proc.response_processors.save_processing_response.v3.SaveProcessingResponse
      filter_state_name_suffix: "test"
      save_request_trailers:
        save_response: true
      save_response_trailers:
        save_response: true
  )EOF");

  HttpTestUtility::addDefaultHeaders(request_headers_);
  request_headers_.setMethod("POST");
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(nullptr));
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  processRequestHeaders(false, absl::nullopt);

  const uint32_t chunk_number = 20;
  sendChunkRequestData(chunk_number, true);
  const std::string filter_state_name =
      absl::StrCat(SaveProcessingResponseFilterState::kFilterStateName, ".test");
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  processRequestTrailers(
      [](const HttpTrailers&, ProcessingResponse&, TrailersResponse& trailers_resp) {
        auto headers_mut = trailers_resp.mutable_header_mutation();
        auto add1 = headers_mut->add_set_headers();
        add1->mutable_header()->set_key("x-new-header1");
        add1->mutable_header()->set_raw_value("new");
        add1->mutable_append()->set_value(false);
        auto add2 = headers_mut->add_set_headers();
        add2->mutable_header()->set_key("x-some-other-header1");
        add2->mutable_header()->set_raw_value("no");
        add2->mutable_append()->set_value(true);
        *headers_mut->add_remove_headers() = "x-do-we-want-this";
      },
      true);
  auto filter_state = stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
      filter_state_name);
  ASSERT_TRUE(filter_state->response.has_value());
  envoy::service::ext_proc::v3::ProcessingResponse expected_response;
  TestUtility::loadFromJson(
      R"EOF(
  {
  "requestTrailers": {
    "headerMutation": {
      "setHeaders": [{
        "header": {
          "key": "x-new-header1",
          "rawValue": "bmV3"
        },
        "append": false
      }, {
        "header": {
          "key": "x-some-other-header1",
          "rawValue": "bm8="
        },
        "append": true
      }],
      "removeHeaders": ["x-do-we-want-this"]
    }
  }
})EOF",
      expected_response);
  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_response));

  filter_state->response.reset();

  response_headers_.addCopy(LowerCaseString(":status"), "200");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->encodeHeaders(response_headers_, false));
  processResponseHeaders(true, absl::nullopt);
  sendChunkResponseData(chunk_number * 2, true);
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
  processResponseTrailers(
      [](const HttpTrailers&, ProcessingResponse&, TrailersResponse& trailers_resp) {
        auto headers_mut = trailers_resp.mutable_header_mutation();
        auto* resp_add1 = headers_mut->add_set_headers();
        resp_add1->mutable_append()->set_value(false);
        resp_add1->mutable_header()->set_key("x-new-header1");
        resp_add1->mutable_header()->set_raw_value("new");
      },
      true);
  processResponseTrailers(absl::nullopt, false);
  envoy::service::ext_proc::v3::ProcessingResponse expected_response_trailers;
  TestUtility::loadFromJson(
      R"EOF(
  {
  "responseTrailers": {
    "headerMutation": {
      "setHeaders": [{
        "header": {
          "key": "x-new-header1",
          "rawValue": "bmV3"
        },
        "append": false
      }]
    }
  }
})EOF",
      expected_response_trailers);
  EXPECT_TRUE(TestUtility::protoEqual(filter_state->response.value().processing_response,
                                      expected_response_trailers));
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

TEST_F(HttpFilterTest, DontSaveProcessingResponse) {
  SaveProcessingResponseFactory factory;
  Envoy::Registry::InjectFactory<OnProcessingResponseFactory> registration(factory);
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
  on_processing_response:
    name: "abc"
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.http.ext_proc.response_processors.save_processing_response.v3.SaveProcessingResponse
  )EOF");

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
  EXPECT_EQ(stream_info_.filterState()->getDataMutable<SaveProcessingResponseFilterState>(
                SaveProcessingResponseFilterState::kFilterStateName),
            nullptr);
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

TEST_F(HttpFilterTest, CloseStreamOnRequestHeaders) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  processing_mode:
    request_header_mode: SEND
    response_header_mode: SKIP
    request_body_mode: NONE
    response_body_mode: NONE
    request_trailer_mode: SKIP
    response_trailer_mode: SKIP
  )EOF");

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, true));
  // The next response should be the last, so expect the stream to be closed.
  processRequestHeaders(true, [](const HttpHeaders&, ProcessingResponse&, HeadersResponse&) {});
  EXPECT_EQ(1, config_->stats().stream_msgs_sent_.value());
  EXPECT_EQ(1, config_->stats().stream_msgs_received_.value());
  EXPECT_EQ(1, config_->stats().streams_closed_.value());
  filter_->onDestroy();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
