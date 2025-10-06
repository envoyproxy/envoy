#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/status.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/extensions/filters/http/ext_proc/filter_test_common.h"
#include "test/mocks/http/mocks.h"
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
using ::envoy::service::ext_proc::v3::ProcessingResponse;

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

using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Unused;

// Test suite for failure_mode_allow override.
class FailureModeAllowOverrideTest : public HttpFilterTest {};

TEST_F(FailureModeAllowOverrideTest, FilterAllowRouteDisallow) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: true)EOF";
  initialize(std::move(yaml_config));

  ExtProcPerRoute route_proto;
  route_proto.mutable_overrides()->mutable_failure_mode_allow()->set_value(false);
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");
  filter_->onDestroy();
}

TEST_F(FailureModeAllowOverrideTest, FilterDisallowRouteAllow) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: false)EOF";
  initialize(std::move(yaml_config));

  ExtProcPerRoute route_proto;
  route_proto.mutable_overrides()->mutable_failure_mode_allow()->set_value(true);
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(filter_->decodeData(req_data, true), FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), FilterTrailersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), FilterHeadersStatus::Continue);
  filter_->onDestroy();

  EXPECT_EQ(config_->stats().failure_mode_allowed_.value(), 1);
}

TEST_F(FailureModeAllowOverrideTest, FilterAllowNoRouteOverride) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: true)EOF";
  initialize(std::move(yaml_config));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(filter_->decodeData(req_data, true), FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), FilterTrailersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), FilterHeadersStatus::Continue);
  filter_->onDestroy();

  EXPECT_EQ(config_->stats().failure_mode_allowed_.value(), 1);
}

TEST_F(FailureModeAllowOverrideTest, FilterDisallowNoRouteOverride) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: false)EOF";
  initialize(std::move(yaml_config));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");
  filter_->onDestroy();
}

TEST_F(FailureModeAllowOverrideTest, FilterAllowRouteUnrelatedOverride) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: true)EOF";
  initialize(std::move(yaml_config));

  ExtProcPerRoute route_proto;
  // This override does not set failure_mode_allow, so the filter's value should still apply.
  route_proto.mutable_overrides()->mutable_processing_mode()->set_response_header_mode(
      ProcessingMode::SKIP);
  FilterConfigPerRoute route_config(route_proto, builder_, factory_context_);
  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(
          testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs { return {&route_config}; }));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");

  Buffer::OwnedImpl req_data("foo");
  EXPECT_EQ(filter_->decodeData(req_data, true), FilterDataStatus::Continue);
  EXPECT_EQ(filter_->decodeTrailers(request_trailers_), FilterTrailersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, true), FilterHeadersStatus::Continue);
  filter_->onDestroy();

  EXPECT_EQ(config_->stats().failure_mode_allowed_.value(), 1);
}

TEST_F(FailureModeAllowOverrideTest, FailureModeAllowMergedOverride) {
  std::string yaml_config = R"EOF(
grpc_service:
  envoy_grpc:
    cluster_name: "ext_proc_server"
failure_mode_allow: true)EOF";
  initialize(std::move(yaml_config));

  ExtProcPerRoute route_proto_less_specific;
  route_proto_less_specific.mutable_overrides()->mutable_failure_mode_allow()->set_value(true);
  FilterConfigPerRoute route_config_less_specific(route_proto_less_specific, builder_,
                                                  factory_context_);

  ExtProcPerRoute route_proto_more_specific;
  route_proto_more_specific.mutable_overrides()->mutable_failure_mode_allow()->set_value(false);
  FilterConfigPerRoute route_config_more_specific(route_proto_more_specific, builder_,
                                                  factory_context_);

  EXPECT_CALL(decoder_callbacks_, perFilterConfigs())
      .WillRepeatedly(testing::Invoke([&]() -> Router::RouteSpecificFilterConfigs {
        return {&route_config_less_specific, &route_config_more_specific};
      }));

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, false), FilterHeadersStatus::StopIteration);
  test_time_->advanceTimeWait(std::chrono::microseconds(10));

  TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(::Envoy::Http::Code::InternalServerError, "", _, Eq(absl::nullopt),
                             "ext_proc_error_gRPC_error_13{error_message}"))
      .WillOnce(Invoke([&immediate_response_headers](
                           Unused, Unused,
                           std::function<void(ResponseHeaderMap & headers)> modify_headers, Unused,
                           Unused) { modify_headers(immediate_response_headers); }));

  server_closed_stream_ = true; // Simulate stream close without proper gRPC response
  stream_callbacks_->onGrpcError(Grpc::Status::Internal, "error_message");
  filter_->onDestroy();
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
  HttpConnectionManagerImplMixin::setup(Envoy::Http::SetupOpts().setServerName("fake-server"));
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
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key("foo");
        hdr->mutable_header()->set_raw_value("gift-from-external-server");
        hdr = headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key(":path");
        hdr->mutable_header()->set_raw_value("/mutated_path/bluh");
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
  HttpConnectionManagerImplMixin::setup(Envoy::Http::SetupOpts().setServerName("fake-server"));
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
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key("foo");
        hdr->mutable_header()->set_raw_value("gift-from-external-server");
        hdr = headers_response->mutable_response()->mutable_header_mutation()->add_set_headers();
        hdr->mutable_append()->set_value(false);
        hdr->mutable_header()->set_key("new_response_header");
        hdr->mutable_header()->set_raw_value("bluh");
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
