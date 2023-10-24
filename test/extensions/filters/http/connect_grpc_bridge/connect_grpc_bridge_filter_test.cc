#include "source/common/common/base64.h"
#include "source/common/grpc/codec.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/connect_grpc_bridge/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
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

  bool jsonEqual(const std::string& expected, const std::string& actual) {
    ProtobufWkt::Value expected_value, actual_value;
    TestUtility::loadFromJson(expected, expected_value);
    TestUtility::loadFromJson(actual, actual_value);
    return TestUtility::protoEqual(expected_value, actual_value);
  }

  void setStatusDetails(Http::HeaderMap& headers, const google::rpc::Status& status) {
    std::string message;
    EXPECT_TRUE(status.SerializeToString(&message));
    auto encoded_value = Base64::encode(message.c_str(), message.size());
    headers.addCopy(Http::Headers::get().GrpcStatusDetailsBin, encoded_value);
  }

  void addStatusDetails(google::rpc::Status& status, const Protobuf::Message& message) {
    ProtobufWkt::Any any;
    any.PackFrom(message);
    *status.add_details() = any;
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
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnsupportedContentType) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("unsupported");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, MissingConnectVersionHeader) {
  request_headers_.addCopy(Http::Headers::get().ContentType,
                           Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnarySupportedContentType) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("application/grpc+proto", request_headers_.getContentTypeValue());
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectProtocolVersion));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutInvalid) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "invalid!");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().GrpcTimeout));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutTranslationMilliseconds) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "10000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("10000m", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutTranslationSeconds) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "32403600000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("32403600S", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutTranslationMinutes) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "1000000000000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("16666666M", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutTranslationHours) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "10000000000000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("2777777H", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTimeoutOutOfRange) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "360000000000000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().GrpcTimeout));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestTranslation) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(
      Http::Headers::get().TEValues.Trailers,
      request_headers_.get_(Http::Headers::get().TE)); // C++ gRPC refuses connections without this.

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ('\0', data.drainInt<uint8_t>());
  EXPECT_EQ(13U, data.drainBEInt<uint32_t>());
  EXPECT_EQ(R"EOF({"request":1})EOF", data.toString());

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke((
          [&](Buffer::Instance& d, bool) { EXPECT_EQ(R"EOF({"response":1})EOF", d.toString()); })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestCompression) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  request_headers_.setCopy(Http::CustomHeaders::get().ContentEncoding, "gzip");
  request_headers_.setCopy(Http::CustomHeaders::get().AcceptEncoding, "brotli");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().ContentEncoding));
  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().AcceptEncoding));
  EXPECT_EQ("gzip", request_headers_.get_(Http::CustomHeaders::get().GrpcEncoding));
  EXPECT_EQ("brotli", request_headers_.get_(Http::CustomHeaders::get().GrpcAcceptEncoding));
  Buffer::OwnedImpl data{"fake"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ('\1', data.drainInt<uint8_t>());
  EXPECT_EQ(4U, data.drainBEInt<uint32_t>());
  EXPECT_EQ("fake", data.toString());
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryResponseCompression) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data{"fake"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  response_headers_.setCopy(Http::CustomHeaders::get().GrpcEncoding, "gzip");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));
  data = {};
  data.writeByte(Grpc::GRPC_FH_COMPRESSED);
  data.writeBEInt<uint32_t>(4);
  data.add("test");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_EQ("gzip", response_headers_.get_(Http::CustomHeaders::get().ContentEncoding));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryResponseCompressionFlagUnset) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data{"fake"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  response_headers_.setCopy(Http::CustomHeaders::get().GrpcEncoding, "gzip");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));
  data = {};
  data.writeByte(Grpc::GRPC_FH_DEFAULT);
  data.writeBEInt<uint32_t>(4);
  data.add("test");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_FALSE(response_headers_.has(Http::CustomHeaders::get().ContentEncoding));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestWithTrailers) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data, false));

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\0', d.drainInt<uint8_t>());
        EXPECT_EQ(13U, d.drainBEInt<uint32_t>());
        EXPECT_EQ(R"EOF({"request":1})EOF", d.toString());
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryDisjointFrameHandling) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Buffer::OwnedImpl part;
  part.move(data, 5);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(part, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ('\0', data.drainInt<uint8_t>());
  EXPECT_EQ(13U, data.drainBEInt<uint32_t>());
  EXPECT_EQ(R"EOF({"request":1})EOF", data.toString());

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);

  // Split the gRPC frame unevenly across buffers to ensure that we always drain only exactly one
  // frame header.
  part = {};
  part.move(data, 3);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));
  part.move(data, 3);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));
  part.move(data);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(part, false));

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke((
          [&](Buffer::Instance& d, bool) { EXPECT_EQ(R"EOF({"response":1})EOF", d.toString()); })));

  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryTrailerErrorTranslation) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));

  google::rpc::Status status;
  status.set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  addStatusDetails(status, ValueUtil::stringValue("Test Status"));
  setStatusDetails(response_trailers_, status);
  response_trailers_.setGrpcStatus(status.code());
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_TRUE(jsonEqual(R"EOF({
          "code": "internal",
          "details": [{
            "type": "type.googleapis.com/google.protobuf.Value",
            "value": "GgtUZXN0IFN0YXR1cw=="
          }]
        })EOF",
                              d.toString()));
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryHeaderOnlyErrorTranslation) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ(R"EOF({"code":"internal"})EOF", d.toString());
      })));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  response_headers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Internal);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, true));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryIgnoreBrokenGrpcResponse) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(500);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));

  data = {};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true)).Times(0);
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryGetRequest) {
  request_headers_.setPath(
      "/Service/Method?connect=v1&encoding=json&message=%7B%22request%22%3A1%7D");
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\0', d.drainInt<uint8_t>());
        EXPECT_EQ(13U, d.drainBEInt<uint32_t>());
        EXPECT_EQ(R"EOF({"request":1})EOF", d.toString());
      })));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, true));
  EXPECT_EQ("/Service/Method", request_headers_.getPathValue());
  EXPECT_EQ(Http::Headers::get().TEValues.Trailers, request_headers_.get_(Http::Headers::get().TE));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.encodeData(data, false));
  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke((
          [&](Buffer::Instance& d, bool) { EXPECT_EQ(R"EOF({"response":1})EOF", d.toString()); })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
  EXPECT_EQ("test", response_headers_.get_("trailer-custom-metadata"));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryGetRequestBase64) {
  request_headers_.setPath(
      "/Service/Method?connect=v1&encoding=json&message=eyJyZXF1ZXN0IjoxfQ&base64=1");
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\0', d.drainInt<uint8_t>());
        EXPECT_EQ(13U, d.drainBEInt<uint32_t>());
        EXPECT_EQ(R"EOF({"request":1})EOF", d.toString());
      })));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, true));
  EXPECT_EQ("application/grpc+json", request_headers_.getContentTypeValue());
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryGetRequestCompression) {
  request_headers_.setPath(
      "/Service/Method?connect=v1&encoding=proto&message=fake&compression=gzip");
  request_headers_.setCopy(Http::CustomHeaders::get().AcceptEncoding, "brotli");
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\1', d.drainInt<uint8_t>());
        EXPECT_EQ(4U, d.drainBEInt<uint32_t>());
        EXPECT_EQ("fake", d.toString());
      })));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, true));
  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().AcceptEncoding));
  EXPECT_EQ("gzip", request_headers_.get_(Http::CustomHeaders::get().GrpcEncoding));
  EXPECT_EQ("brotli", request_headers_.get_(Http::CustomHeaders::get().GrpcAcceptEncoding));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryGetRequestTimeout) {
  request_headers_.setPath("/Service/Method?connect=v1&encoding=proto&message=fake");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "1000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, true));
  EXPECT_EQ("1000m", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestWithNoBodyNorTrailers) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType("application/proto");

  Buffer::OwnedImpl data{};

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
      .WillOnce(
          Invoke(([&](Buffer::Instance& d, bool) { EXPECT_EQ('\0', d.drainInt<uint8_t>()); })));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, true));
}

TEST_F(ConnectGrpcBridgeFilterTest, UnaryRequestRemovesContentLength) {
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectProtocolVersion, "1");
  request_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  request_headers_.setContentLength(1337);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("", request_headers_.get_("content-length"));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingSupportedContentType) {
  request_headers_.setContentType("application/connect+proto");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("application/grpc+proto", request_headers_.getContentTypeValue());
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingTimeoutTranslation) {
  request_headers_.setContentType("application/connect+proto");
  request_headers_.addCopy(Http::CustomHeaders::get().ConnectTimeoutMs, "1000");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("1000m", request_headers_.get_(Http::CustomHeaders::get().GrpcTimeout));
  EXPECT_EQ(false, request_headers_.has(Http::CustomHeaders::get().ConnectTimeoutMs));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingRequestTranslation) {
  request_headers_.setContentType("application/connect+json");
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectContentEncoding, "gzip");
  request_headers_.setCopy(Http::CustomHeaders::get().ConnectAcceptEncoding, "brotli");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(
      Http::Headers::get().TEValues.Trailers,
      request_headers_.get_(Http::Headers::get().TE)); // C++ gRPC refuses connections without this.
  EXPECT_EQ("gzip", request_headers_.get_(Http::CustomHeaders::get().GrpcEncoding));
  EXPECT_EQ("brotli", request_headers_.get_(Http::CustomHeaders::get().GrpcAcceptEncoding));
  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().ConnectContentEncoding));
  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().ConnectAcceptEncoding));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(200);
  response_headers_.setContentType("application/grpc+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ("application/connect+json", response_headers_.get_(Http::Headers::get().ContentType));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));

  response_trailers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Ok);
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\2', d.drainInt<uint8_t>());
        EXPECT_EQ(41U, d.drainBEInt<uint32_t>());
        EXPECT_TRUE(jsonEqual(R"EOF({
          "metadata": {
            "custom-metadata": ["test"]
          }
        })EOF",
                              d.toString()));
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingErrorTranslation) {
  request_headers_.setContentType("application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));

  data = {R"EOF({"response":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));

  google::rpc::Status status;
  status.set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  addStatusDetails(status, ValueUtil::stringValue("Test Status"));
  setStatusDetails(response_trailers_, status);
  response_trailers_.setGrpcStatus(status.code());
  response_trailers_.addCopy("custom-metadata", "test");
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\2', d.drainInt<uint8_t>());
        d.drainBEInt<uint32_t>();
        EXPECT_TRUE(jsonEqual(R"EOF({
          "metadata": {
            "custom-metadata": ["test"]
          },
          "error": {
            "code": "internal",
            "details": [{
              "type": "type.googleapis.com/google.protobuf.Value",
              "value": "GgtUZXN0IFN0YXR1cw=="
            }]
          }
        })EOF",
                              d.toString()));
      })));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingHeaderOnlyErrorTranslation) {
  request_headers_.setContentType("application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke(([&](Buffer::Instance& d, bool) {
        EXPECT_EQ('\2', d.drainInt<uint8_t>());
        EXPECT_EQ(29U, d.drainBEInt<uint32_t>());
        EXPECT_EQ(R"EOF({"error":{"code":"internal"}})EOF", d.toString());
      })));

  response_headers_.setStatus(200);
  response_headers_.setContentType(Http::Headers::get().ContentTypeValues.Grpc);
  response_headers_.setGrpcStatus(Grpc::Status::WellKnownGrpcStatus::Internal);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, true));
}

TEST_F(ConnectGrpcBridgeFilterTest, StreamingIgnoreBrokenGrpcResponse) {
  request_headers_.setContentType("application/connect+json");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data{R"EOF({"request":1})EOF"};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));

  response_headers_.setStatus(500);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));

  data = {};
  Grpc::Common::prependGrpcFrameHeader(data);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, true));
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true)).Times(0);
}

} // namespace
} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
