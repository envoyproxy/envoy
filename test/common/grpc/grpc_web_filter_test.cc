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
using testing::Invoke;

namespace Envoy {
namespace Grpc {
namespace {
const char MESSAGE[] = "\x00\x00\x00\x00\x11grpc-web-bin-data";
const size_t MESSAGE_SIZE = 22;
const char TEXT_MESSAGE[] = "\x00\x00\x00\x00\x12grpc-web-text-data";
const size_t TEXT_MESSAGE_SIZE = 23;
const char B64_MESSAGE[] = "AAAAABJncnBjLXdlYi10ZXh0LWRhdGE=";
const size_t B64_MESSAGE_SIZE = 32;
const char TRAILERS[] = "\x80\x00\x00\x00\x0fgrpc-status:0\r\n";
const size_t TRAILERS_SIZE = 20;
} // namespace

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
  Buffer::OwnedImpl request_buffer(MESSAGE, MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
  EXPECT_EQ(0, memcmp(MESSAGE, TestUtility::bufferToString(request_buffer).c_str(), MESSAGE_SIZE));

  // Tests response headers.
  Http::TestHeaderMapImpl response_headers;
  response_headers.addViaCopy(Http::Headers::get().Status, "200");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("200", response_headers.get_(Http::Headers::get().Status.get()));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWeb,
            response_headers.ContentType()->value().c_str());

  // Tests response data.
  Buffer::OwnedImpl response_buffer(MESSAGE, MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
  EXPECT_EQ(0, memcmp(MESSAGE, TestUtility::bufferToString(response_buffer).c_str(), MESSAGE_SIZE));
  response_buffer.drain(response_buffer.length());

  // Tests response trailers.
  Buffer::OwnedImpl trailers_buffer;
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) { trailers_buffer.move(data); }));
  Http::TestHeaderMapImpl response_trailers;
  response_trailers.addViaCopy(Http::Headers::get().GrpcStatus, "0");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(0,
            memcmp(TRAILERS, TestUtility::bufferToString(trailers_buffer).c_str(), TRAILERS_SIZE));
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
  Buffer::OwnedImpl request_buffer(B64_MESSAGE, B64_MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
  EXPECT_EQ(0, memcmp(TEXT_MESSAGE, TestUtility::bufferToString(request_buffer).c_str(),
                      TEXT_MESSAGE_SIZE));

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
  Buffer::OwnedImpl response_buffer(TEXT_MESSAGE, TEXT_MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
  EXPECT_EQ(0, memcmp(B64_MESSAGE, TestUtility::bufferToString(response_buffer).c_str(),
                      B64_MESSAGE_SIZE));
  response_buffer.drain(response_buffer.length());

  // Tests response trailers.
  Buffer::OwnedImpl trailers_buffer;
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) { trailers_buffer.move(data); }));
  Http::TestHeaderMapImpl response_trailers;
  response_trailers.addViaCopy(Http::Headers::get().GrpcStatus, "0");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(0,
            memcmp(TRAILERS, TestUtility::bufferToString(trailers_buffer).c_str(), TRAILERS_SIZE));
}
} // namespace Grpc
} // namespace Envoy
