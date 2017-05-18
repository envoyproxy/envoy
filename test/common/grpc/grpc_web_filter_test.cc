#include "common/buffer/buffer_impl.h"
#include "common/common/base64.h"
#include "common/grpc/grpc_web_filter.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {

class GrpcWebFilterTest : public testing::Test {
public:
  GrpcWebFilterTest() : filter_() { filter_.setEncoderFilterCallbacks(encoder_callbacks_); }

  ~GrpcWebFilterTest() override {}

  GrpcWebFilter filter_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(GrpcWebFilterTest, BinaryUnary) {
  // Tests request headers.
  Http::TestHeaderMapImpl request_headers;
  request_headers.addViaCopy(Http::Headers::get().ContentType,
                             Http::Headers::get().ContentTypeValues.GrpcWeb);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            request_headers.ContentType()->value().c_str());

  // Tests request data.
  Buffer::OwnedImpl request_buffer("\x00\x00\x00\x00\x11grpc-web-bin-data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
  EXPECT_EQ(0, strncmp("\x00\x00\x00\x00\x11grpc-web-bin-data",
                       TestUtility::bufferToString(request_buffer).c_str(), 22));

  // Tests response headers.
  Http::TestHeaderMapImpl response_headers;
  response_headers.addViaCopy(Http::Headers::get().Status, "200");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("200", response_headers.get_(Http::Headers::get().Status.get()));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWeb,
            response_headers.ContentType()->value().c_str());

  // Tests response data.
  Buffer::OwnedImpl response_buffer("\x00\x00\x00\x00\x11grpc-web-bin-data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
  EXPECT_EQ("\x00\x00\x00\x00\x11grpc-web-bin-data", TestUtility::bufferToString(response_buffer));
  response_buffer.drain(response_buffer.length());

  // Tests response trailers.
  Http::TestHeaderMapImpl response_trailers;
  response_trailers.addViaCopy(Http::Headers::get().GrpcStatus, "0");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(0, strncmp("\x80\0x00\0x00\0x00\0x23grpc-status:0\r\n",
                       reinterpret_cast<char*>(response_buffer.linearize(response_buffer.length())),
                       response_buffer.length()));
}

TEST_F(GrpcWebFilterTest, TextUnary) {
  // Tests request headers.
  Http::TestHeaderMapImpl request_headers;
  request_headers.addViaCopy(Http::Headers::get().ContentType,
                             Http::Headers::get().ContentTypeValues.GrpcWebText);
  request_headers.addViaCopy(Http::Headers::get().Accept,
                             Http::Headers::get().ContentTypeValues.GrpcWebText);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            request_headers.ContentType()->value().c_str());

  // Tests request data.
  std::string data("\x00\x00\x00\x00\x12grpc-web-text-data", 23);
  std::string b64_data("AAAAABJncnBjLXdlYi10ZXh0LWRhdGE=");
  Buffer::OwnedImpl request_buffer(b64_data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
  EXPECT_EQ(data, TestUtility::bufferToString(request_buffer));

  // Tests response headers.
  Http::TestHeaderMapImpl response_headers;
  response_headers.addViaCopy(Http::Headers::get().Status, "200");
  response_headers.addViaCopy(Http::Headers::get().ContentType,
                              Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("200", response_headers.get_(Http::Headers::get().Status.get()));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWebText,
            response_headers.ContentType()->value().c_str());

  // Tests response data.
  Buffer::OwnedImpl response_buffer(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
  EXPECT_EQ(b64_data, TestUtility::bufferToString(response_buffer));
  response_buffer.drain(response_buffer.length());

  // Tests response trailers.
  Http::TestHeaderMapImpl response_trailers;
  response_trailers.addViaCopy(Http::Headers::get().GrpcStatus, "0");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(0, strncmp("\x80\0x00\0x00\0x00\0x23grpc-status:0\r\n",
                       reinterpret_cast<char*>(response_buffer.linearize(response_buffer.length())),
                       response_buffer.length()));
}
} // namespace Grpc
} // namespace Envoy
