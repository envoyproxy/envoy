#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/grpc/status.h"
#include "envoy/http/async_client.h"
#include "envoy/router/router.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/sidestream_watermark.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::TestResponseTrailerMapImpl;
using ::envoy::service::ext_proc::v3::BodyResponse;
using ::envoy::service::ext_proc::v3::HeadersResponse;
using ::envoy::service::ext_proc::v3::HttpBody;
using ::envoy::service::ext_proc::v3::HttpHeaders;
using ::envoy::service::ext_proc::v3::HttpTrailers;
using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ::envoy::service::ext_proc::v3::TrailersResponse;
using ::testing::NiceMock;
using ::testing::Unused;

// These tests are all unit tests that directly drive an instance of the
// ext_proc filter and verify the behavior using mocks.

class HttpFilterTest : public testing::Test {
protected:
  void initialize(std::string&& yaml, bool is_upstream_filter = false);
  void initializeTestSendAll();
  void TearDown() override;
  bool allTimersDisabled();

  ExternalProcessorStreamPtr doStart(ExternalProcessorCallbacks& callbacks,
                                     const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                                     const Envoy::Http::AsyncClient::StreamOptions&,
                                     Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&);

  void doSetDynamicMetadata(const std::string& ns, const Protobuf::Struct& val) {
    (*dynamic_metadata_.mutable_filter_metadata())[ns] = val;
  };

  void doSend(ProcessingRequest&& request, Unused) { last_request_ = std::move(request); }

  bool doSendClose() { return !server_closed_stream_; }

  void setUpDecodingBuffering(Buffer::Instance& buf, bool expect_modification = false);

  void setUpEncodingBuffering(Buffer::Instance& buf, bool expect_modification = false);

  void setUpDecodingWatermarking(bool& watermarked);

  void setUpEncodingWatermarking(bool& watermarked);

  // Expect a request_headers request, and send back a valid response.
  void processRequestHeaders(
      bool buffering_data,
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
          cb);

  // Expect a response_headers request, and send back a valid response
  void processResponseHeaders(
      bool buffering_data,
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
          cb);

  void processResponseHeadersAfterTrailer(
      absl::optional<std::function<void(const HttpHeaders&, ProcessingResponse&, HeadersResponse&)>>
          cb);

  // Expect a request_body request, and send back a valid response
  void processRequestBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true,
      const std::chrono::microseconds latency = std::chrono::microseconds(10));

  // Expect a request_body request, and send back a valid response
  void processResponseBody(
      absl::optional<std::function<void(const HttpBody&, ProcessingResponse&, BodyResponse&)>> cb,
      bool should_continue = true);

  void processResponseBodyHelper(absl::string_view data, Buffer::OwnedImpl& want_response_body,
                                 bool end_of_stream = false, bool should_continue = false);

  void processResponseBodyStreamedAfterTrailer(absl::string_view data,
                                               Buffer::OwnedImpl& want_response_body);

  void processRequestTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true);

  void processResponseTrailers(
      absl::optional<
          std::function<void(const HttpTrailers&, ProcessingResponse&, TrailersResponse&)>>
          cb,
      bool should_continue = true);

  // Get the gRPC call stats data from the filter state.
  const ExtProcLoggingInfo::GrpcCalls&
  getGrpcCalls(const envoy::config::core::v3::TrafficDirection traffic_direction);

  // Check gRPC call stats for headers and trailers.
  void checkGrpcCall(const ExtProcLoggingInfo::GrpcCall call,
                     const std::chrono::microseconds latency,
                     const Grpc::Status::GrpcStatus call_status);

  // Check gRPC call stats for body.
  void checkGrpcCallBody(const ExtProcLoggingInfo::GrpcCallBody call, const uint32_t call_count,
                         const Grpc::Status::GrpcStatus call_status,
                         const std::chrono::microseconds total_latency,
                         const std::chrono::microseconds max_latency,
                         const std::chrono::microseconds min_latency);

  // Verify gRPC calls only happened on headers.
  void
  checkGrpcCallHeaderOnlyStats(const envoy::config::core::v3::TrafficDirection traffic_direction,
                               const Grpc::Status::GrpcStatus call_status = Grpc::Status::Ok);

  // Verify gRPC calls for  headers, body, and trailer.
  void checkGrpcCallStatsAll(const envoy::config::core::v3::TrafficDirection traffic_direction,
                             const uint32_t body_chunk_number,
                             const Grpc::Status::GrpcStatus body_call_status = Grpc::Status::Ok,
                             const bool trailer_stats = true);

  // Verify no gRPC call happened.
  void expectNoGrpcCall(const envoy::config::core::v3::TrafficDirection traffic_direction);

  void sendChunkRequestData(const uint32_t chunk_number, const bool send_grpc);

  void sendChunkResponseData(const uint32_t chunk_number, const bool send_grpc);

  void streamingSmallChunksWithBodyMutation(bool empty_last_chunk, bool mutate_last_chunk);

  // The metadata configured as part of ext_proc filter should be in the filter state.
  // In addition, bytes sent/received should also be stored.
  void expectFilterState(const Envoy::Protobuf::Struct& expected_metadata);

  absl::optional<envoy::config::core::v3::GrpcService> final_expected_grpc_service_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  std::unique_ptr<MockClient> client_;
  ExternalProcessorCallbacks* stream_callbacks_ = nullptr;
  ProcessingRequest last_request_;
  bool server_closed_stream_ = false;
  bool observability_mode_ = false;
  testing::NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder_;
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
  TestScopedRuntime scoped_runtime_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
