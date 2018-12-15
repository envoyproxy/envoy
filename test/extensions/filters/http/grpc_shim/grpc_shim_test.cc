#include <netinet/in.h>

#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/grpc_shim/grpc_shim.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;
using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcShim {

class GrpcShimTest : public testing::Test {
protected:
  void initialize() {
    filter_.reset(new GrpcShim("application/x-protobuf"));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::unique_ptr<GrpcShim> filter_;
  std::shared_ptr<Router::MockRoute> route_ = std::make_shared<Router::MockRoute>();
  Router::RouteSpecificFilterConfig filter_config_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
};

// Tests that the filter passes a non-GRPC request through without modification.
TEST_F(GrpcShimTest, NoGrpcRequest) {
  initialize();

  {
    Http::TestHeaderMapImpl headers({{"content-type", "application/json"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    // Ensure we didn't mutate content type.
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/json"));
  }

  {
    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("test", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    EXPECT_EQ(4, buffer.length());
  }

  Http::TestHeaderMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestHeaderMapImpl headers({{"content-type", "application/json"}});
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  // Ensure we didn't mutate content type.
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/json"));

  Envoy::Buffer::OwnedImpl buffer;
  buffer.add("test", 4);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
  EXPECT_EQ(4, buffer.length());
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC.
TEST_F(GrpcShimTest, GrpcRequest) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, clearRouteCache());
    Http::TestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().Accept, "application/x-protobuf"));
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
    Http::TestHeaderMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestHeaderMapImpl headers({{":status", "200"}, {"content-type", "application/x-protobuf"}});
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
    // Last call should prefix the buffer with the size and insert the gRPC status into trailers.
    Http::TestHeaderMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_EQ(17, buffer.length());
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

    char null;
    buffer.copyOut(0, 1, &null);
    EXPECT_EQ('\0', null);
    uint32_t length;
    buffer.copyOut(1, 5, &length);

    EXPECT_EQ(12, ntohl(length));
  }
}

// Tests that a gRPC is downgraded to application/x-protobuf and upgraded back
// to gRPC, and that the upstream 400 is converted into an internal (13)
// grpc-status.
TEST_F(GrpcShimTest, GrpcRequestInternalError) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, clearRouteCache());
    Http::TestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().Accept, "application/x-protobuf"));
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
    Http::TestHeaderMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  }

  Http::TestHeaderMapImpl headers({{":status", "400"}, {"content-type", "application/x-protobuf"}});
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
    Http::TestHeaderMapImpl trailers;
    EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

    Envoy::Buffer::OwnedImpl buffer;
    buffer.add("ghj", 4);
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    EXPECT_THAT(trailers, HeaderValueOf(Http::Headers::get().GrpcStatus, "13"));
    char null;
    buffer.copyOut(0, 1, &null);
    EXPECT_EQ('\0', null);
    uint32_t length;
    buffer.copyOut(1, 5, &length);

    EXPECT_EQ(12, ntohl(length));
  }
}

// Tests that a gRPC is downgraded to application/x-protobuf and that if the response
// has an invalid content type we respond with a useful error message.
TEST_F(GrpcShimTest, GrpcRequestBadResponse) {
  initialize();
  decoder_callbacks_.is_grpc_request_ = true;

  {
    EXPECT_CALL(decoder_callbacks_, clearRouteCache());
    Http::TestHeaderMapImpl headers(
        {{"content-type", "application/grpc"}, {":path", "/testing.ExampleService/SendData"}});
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
    EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().Accept, "application/x-protobuf"));
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

  Http::TestHeaderMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  Http::TestHeaderMapImpl headers({{":status", "400"}, {"content-type", "application/json"}});
  EXPECT_EQ(Http::FilterHeadersStatus::ContinueAndEndStream,
            filter_->encodeHeaders(headers, false));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().Status, "200"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().GrpcStatus, "2"));
  EXPECT_THAT(headers, HeaderValueOf(Http::Headers::get().GrpcMessage,
                                     "envoy grpc-shim: upstream responded with unsupported "
                                     "content-type application/json, status code 400"));
}
} // namespace GrpcShim
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
