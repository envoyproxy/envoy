#include "source/extensions/filters/http/connect_grpc_bridge/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {
namespace {

class ConnectGrpcUnaryFilterTest : public testing::Test {
protected:
  ConnectGrpcUnaryFilterTest() {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

public:
  ConnectUnaryToGrpcFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Http::TestRequestHeaderMapImpl request_headers_{{":path", "/Service/Method"}};
};

TEST_F(ConnectGrpcUnaryFilterTest, NoContentType) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcUnaryFilterTest, UnsupportedContentType) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.addCopy(Http::Headers::get().ContentType, "unsupported");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcUnaryFilterTest, MissingConnectVersionHeader) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::Headers::get().ContentType,
                           Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcUnaryFilterTest, SupportedContentType) {
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/proto");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("application/grpc+proto", request_headers_.getContentTypeValue());
}

} // namespace
} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
