#include <memory>
#include <string>

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/grpc_http1_reverse_bridge/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;
using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {
namespace {

class ReverseBridgeTest : public testing::Test {
protected:
  void initialize(bool withhold_grpc_headers = true, std::string custom_response_size_header = "") {
    filter_ = std::make_unique<Filter>("application/x-protobuf", withhold_grpc_headers,
                                       custom_response_size_header);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  FilterPtr filter_;
  std::shared_ptr<Router::MockRoute> route_ = std::make_shared<Router::MockRoute>();
  Router::RouteSpecificFilterConfig filter_config_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

// Verifies that an incoming request with too small a request body will immediately fail.
TEST_F(ReverseBridgeTest, InvalidGrpcRequest) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 3);
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _));
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _)).WillOnce(Invoke([](auto& headers, auto) {
      EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().Status, "200"));
      EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().GrpcStatus, "2"));
      EXPECT_THAT(headers,
                  HeaderValueOf(Http::Headers::get().GrpcMessage,
                                Http::Utility::PercentEncoding::encode("invalid request body")));
    }));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, false));
    EXPECT_EQ(decoder_callbacks_.details(), "grpc_bridge_data_too_small");
  }
}

// Verifies that we do nothing to a header only request even if it looks like a gRPC request.
TEST_F(ReverseBridgeTest, HeaderOnlyGrpcRequest) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));

    // Verify that headers are unmodified.
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "25"));
  }

  // Verify no modification on encoding path as well.
  Http::TestResponseHeaderMapImpl headers(
      {{"content-type", "application/grpc"}, {"content-length", "20"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
  // Ensure we didn't mutate content type or length.
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));

  // We should not drain the buffer, nor stop iteration.
  Envoy::Buffer::OwnedImpl buffer;
  buffer.add("abc", 3);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(3, buffer.length());
}

// Tests that the filter passes a non-GRPC request through without modification.
TEST_F(ReverseBridgeTest, NoGrpcRequest) {
  initialize();

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/json"}, {"content-length", "10"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    // Ensure we didn't mutate content type or length.
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/json"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "10"));
  }

  {
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("test", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ(4, buffer.length());
  }

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  {
    Http::TestResponseHeaderMapImpl headers(
        {{"content-type", "application/json"}, {"content-length", "20"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
    // Ensure we didn't mutate content type or length.
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/json"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
  }

  Envoy::Buffer::OwnedImpl buffer;
  buffer.add("test", 4);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  EXPECT_EQ(4, buffer.length());

  // Verify no modification on encoding path as well.
  Http::TestResponseHeaderMapImpl headers(
      {{"content-type", "application/grpc"}, {"content-length", "20"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
  // Ensure we didn't mutate content type or length.
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
}

// Verifies that if we receive a gRPC request but have configured the filter to not handle the gRPC
// frames, then the data should not be modified.
TEST_F(ReverseBridgeTest, GrpcRequestNoManageFrameHeader) {
  initialize(false);
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "25"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should not mutate the request data.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  // We should not modify the content-length.
  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-length", "30"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "30"));

  {
    // We should not drain the buffer, nor stop iteration.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 3);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
    EXPECT_EQ(3, buffer.length());
  }

  {
    // Last call should also not modify the buffer.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 3);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(3, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));
  }
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC.
TEST_F(ReverseBridgeTest, GrpcRequest) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-length", "30"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "35"));

  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("def", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // Last call should prefix the buffer with the size and insert the gRPC status into trailers.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(17, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> frames;
    decoder.decode(buffer, frames);

    EXPECT_EQ(1, frames.size());
    EXPECT_EQ(12, frames[0].length_);
  }
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC and that content length headers are not required.
// Same as ReverseBridgeTest.GrpcRequest except no content-length header is passed.
TEST_F(ReverseBridgeTest, GrpcRequestNoContentLength) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
    // Ensure that we don't insert a content-length header.
    EXPECT_EQ(nullptr, headers.ContentLength());
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  // Ensure that we don't insert a content-length header.
  EXPECT_EQ(nullptr, headers.ContentLength());

  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("def", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // Last call should prefix the buffer with the size and insert the gRPC status into trailers.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(17, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> frames;
    decoder.decode(buffer, frames);

    EXPECT_EQ(1, frames.size());
    EXPECT_EQ(12, frames[0].length_);
  }
}

// Regression tests that header-only responses do not get the content-length
// adjusted (https://github.com/envoyproxy/envoy/issues/11099)
TEST_F(ReverseBridgeTest, GrpcRequestHeaderOnlyResponse) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-length", "0"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "0"));
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC, and that the upstream 400 is converted into an internal (13)
// grpc-status.
TEST_F(ReverseBridgeTest, GrpcRequestInternalError) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "400"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));

  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("def", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // Last call should prefix the buffer with the size and insert the appropriate gRPC status.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "13"));

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> frames;
    decoder.decode(buffer, frames);

    EXPECT_EQ(1, frames.size());
    EXPECT_EQ(12, frames[0].length_);
  }
}

// Tests that a gRPC is downgraded to application/x-protobuf and that if the response
// has a missing content type we respond with a useful error message.
TEST_F(ReverseBridgeTest, GrpcRequestBadResponseNoContentType) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestResponseHeaderMapImpl headers({{":status", "400"}});
  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Http::Code::OK,
          "envoy reverse bridge: upstream responded with no content-type header, status code 400",
          _, absl::make_optional(static_cast<Grpc::Status::GrpcStatus>(Grpc::Status::Unknown)), _));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->encodeHeaders(headers, false));
}

// Tests that a gRPC is downgraded to application/x-protobuf and that if the response
// has an invalid content type we respond with a useful error message.
TEST_F(ReverseBridgeTest, GrpcRequestBadResponse) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "400"}, {"content-type", "application/json"}});
  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Http::Code::OK,
          "envoy reverse bridge: upstream responded with unsupported "
          "content-type application/json, status code 400",
          _, absl::make_optional(static_cast<Grpc::Status::GrpcStatus>(Grpc::Status::Unknown)), _));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->encodeHeaders(headers, false));
}

// Tests that the filter passes a GRPC request through without modification because it is disabled
// per route.
TEST_F(ReverseBridgeTest, FilterConfigPerRouteDisabled) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute
      filter_config_per_route;
  filter_config_per_route.set_disabled(true);
  FilterConfigPerRoute filterConfigPerRoute(filter_config_per_route);

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&filterConfigPerRoute));

  EXPECT_CALL(decoder_callbacks_, route()).Times(2);

  Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                          {"content-length", "25"},
                                          {":path", "/testing.ExampleService/SendData"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

  // Verify that headers are unmodified.
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "25"));
  EXPECT_THAT(headers,
              HeaderValueOf(Http::Headers::get().Path, "/testing.ExampleService/SendData"));
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC when the filter is enabled per route.
TEST_F(ReverseBridgeTest, FilterConfigPerRouteEnabled) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute
      filter_config_per_route;
  filter_config_per_route.set_disabled(false);
  FilterConfigPerRoute filterConfigPerRoute(filter_config_per_route);

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&filterConfigPerRoute));

  {
    EXPECT_CALL(decoder_callbacks_, route()).Times(2);
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-length", "30"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "35"));

  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("def", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // Last call should prefix the buffer with the size and insert the gRPC status into trailers.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(17, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> frames;
    decoder.decode(buffer, frames);

    EXPECT_EQ(1, frames.size());
    EXPECT_EQ(12, frames[0].length_);
  }
}

TEST_F(ReverseBridgeTest, RouteWithTrailers) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute
      filter_config_per_route;
  filter_config_per_route.set_disabled(false);
  FilterConfigPerRoute filterConfigPerRoute(filter_config_per_route);

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(testing::Return(&filterConfigPerRoute));

  {
    EXPECT_CALL(decoder_callbacks_, route()).Times(2);
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-length", "30"}, {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "35"));

  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }
  {
    // First few calls should drain the buffer
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("def", 4);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(buffer, false));
    EXPECT_EQ(0, buffer.length());
  }

  {
    // Last call should prefix the buffer with the size and insert the gRPC status into trailers.
    Envoy::Buffer::OwnedImpl buffer;
    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
        .WillOnce(Invoke([&](Envoy::Buffer::Instance& buf, bool) -> void { buffer.move(buf); }));
    Http::TestResponseTrailerMapImpl trailers({{"foo", "bar"}, {"one", "two"}, {"three", "four"}});
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> frames;
    decoder.decode(buffer, frames);

    EXPECT_EQ(4, trailers.size());
    EXPECT_EQ(1, frames.size());
    EXPECT_EQ(8, frames[0].length_);
  }
}

// Verifies that the filter streams responses when it's configured to set the content length based
// on a header returned from the upstream.
TEST_F(ReverseBridgeTest, WithholdGrpcStreamResponse) {
  initialize(true, "custom-content-length");
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers({{"content-type", "application/grpc"},
                                            {"content-length", "25"},
                                            {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "20"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  {
    Http::TestRequestTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"},
       // This is the total length of the 2 buffers encoded separately below.
       {"custom-content-length", "8"},
       {"content-type", "application/x-protobuf"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "13"));

  {
    // The response data should be streamed to the client instead of buffered. Additionally, the
    // first call should prefix the buffer with the size.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
    EXPECT_EQ(9, buffer.length());
  }
  {
    // The last call should insert the gRPC status into trailers. We've already sent the gRPC frame
    // header, so the buffer should only contain the upstream response payload.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(4, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));
  }
}

// Verifies that the filter returns a useful error message when it's configured to set the content
// length based on a header returned from the upstream that is missing.
TEST_F(ReverseBridgeTest, WithholdGrpcStreamResponseNoContentLength) {
  initialize(true, "custom-content-length");
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"}, {"content-type", "application/x-protobuf"}});
  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Http::Code::OK, "envoy reverse bridge: upstream did not set content length", _,
          absl::make_optional(static_cast<Grpc::Status::GrpcStatus>(Grpc::Status::Internal)), _));
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->encodeHeaders(headers, false));
}

// Verifies that the filter returns a useful error message when it's configured to set the content
// length based on a header returned from the upstream that indicates the wrong content length.
TEST_F(ReverseBridgeTest, WithholdGrpcStreamResponseWrongContentLength) {
  initialize(true, "custom-content-length");
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, route()).WillRepeatedly(testing::Return(nullptr));
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    Http::TestRequestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers,
                HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));
  }

  {
    // We should remove the first five bytes.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("fgh", buffer.toString());
  }

  {
    // Subsequent calls to decodeData should do nothing.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abcdefgh", 8);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ("abcdefgh", buffer.toString());
  }

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestResponseHeaderMapImpl headers(
      {{":status", "200"},
       // This is not the correct size of the upstream response payload, since we only send 8 bytes
       // before ending the stream below.
       {"custom-content-length", "30"},
       {"content-type", "application/x-protobuf"}});

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentLength, "35"));

  {
    // The response data should be streamed to the client instead of buffered. Additionally, the
    // first call should prefix the buffer with the size.
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("abc", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
    EXPECT_EQ(9, buffer.length());
  }
  {
    // The last call should insert the gRPC status into trailers. We've already sent the gRPC frame
    // header, so the buffer should only contain the upstream response payload.
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_CALL(
        encoder_callbacks_,
        sendLocalReply(
            Http::Code::OK, "envoy reverse bridge: upstream set incorrect content length", _,
            absl::make_optional(static_cast<Grpc::Status::GrpcStatus>(Grpc::Status::Internal)), _));
    EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(buffer, true));
    EXPECT_EQ(4, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));
  }
}
} // namespace
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
