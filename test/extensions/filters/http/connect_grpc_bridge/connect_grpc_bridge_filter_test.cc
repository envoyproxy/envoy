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

class ConnectGrpcBridgeFilterTest : public testing::Test {
protected:
  ConnectGrpcBridgeFilterTest() {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

public:
  ConnectGrpcBridgeFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Http::TestRequestHeaderMapImpl request_headers_{{":path", "/Service/Method"}};
};

TEST_F(ConnectGrpcBridgeFilterTest, NoContentType) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnsupportedContentType) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("unsupported");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, MissingConnectVersionHeader) {
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

TEST_F(ConnectGrpcBridgeFilterTest, UnarySupportedContentType) {
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("application/grpc+proto", request_headers_.getContentTypeValue());
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectProtocolVersion));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutTranslation) {
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "10000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("10000m", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestTranslation) {
  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ('\0', data.drainInt<uint8_t>());
  EXPECT_EQ(13U, data.drainBEInt<uint32_t>());
  EXPECT_EQ(R"EOF({"request":1})EOF", data.toString());

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke((
          [&](Buffer::Instance& d, bool) { EXPECT_EQ(R"EOF({"response":1})EOF", d.toString()); })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryDisjointResponseFrameDrain) {
  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ('\0', data.drainInt<uint8_t>());
  EXPECT_EQ(13U, data.drainBEInt<uint32_t>());
  EXPECT_EQ(R"EOF({"request":1})EOF", data.toString());

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));

  // Split the gRPC frame unevenly across buffers to ensure that we always drain only exactly one
  // frame header.
  Buffer::OwnedImpl part;
  part.move(data, 3);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));
  part.move(data, 3);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));
  part.move(data);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));

  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke((
          [&](Buffer::Instance& d, bool) { EXPECT_EQ(R"EOF({"response":1})EOF", d.toString()); })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryErrorTranslation) {
  Buffer::OwnedImpl data{};
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  data = {};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Internal);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ(R"EOF({"code":"internal"})EOF", d.toString());
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryIgnoreBrokenGrpcResponse) {
  Buffer::OwnedImpl data{};
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  data = {};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(500);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true)).Times(0);
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingSupportedContentType) {
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/connect+proto");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("application/grpc+proto", request_headers_.getContentTypeValue());
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingTimeoutTranslation) {
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/connect+proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "32403600000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("32403600S", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingRequestTranslation) {
  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\2', d.drainInt<uint8_t>());
        EXPECT_EQ(41U, d.drainBEInt<uint32_t>());
        // The order of fields is not deterministic, so we make use of protobuf equality.
        ProtobufWkt::Value expected_value, actual_value;
        TestUtility::loadFromJson(R"EOF({"metadata":{"custom-metadata":["test"]}})EOF",
                                  expected_value);
        TestUtility::loadFromJson(d.toString(), actual_value);
        EXPECT_TRUE(TestUtility::protoEqual(expected_value, actual_value));
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingErrorTranslation) {
  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Internal);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\2', d.drainInt<uint8_t>());
        EXPECT_EQ(69U, d.drainBEInt<uint32_t>());
        ProtobufWkt::Value expected_value, actual_value;
        TestUtility::loadFromJson(
            R"EOF({"error":{"code":"internal"},"metadata":{"custom-metadata":["test"]}})EOF",
            expected_value);
        TestUtility::loadFromJson(d.toString(), actual_value);
        EXPECT_TRUE(TestUtility::protoEqual(expected_value, actual_value));
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingIgnoreBrokenGrpcResponse) {
  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  request_headers_.addCopy(Http::Headers::get().ContentType, "application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  data = {};
  Grpc::Common::prependGrpcFrameHeader(data);
  response_headers_.setStatus(500);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true)).Times(0);
}

} // namespace
} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
