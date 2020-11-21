#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/http/grpc_web/grpc_web_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Combine;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {
namespace {

const char MESSAGE[] = "\x00\x00\x00\x00\x11grpc-web-bin-data";
const size_t MESSAGE_SIZE = sizeof(MESSAGE) - 1;
const char TEXT_MESSAGE[] = "\x00\x00\x00\x00\x12grpc-web-text-data";
const size_t TEXT_MESSAGE_SIZE = sizeof(TEXT_MESSAGE) - 1;
const char B64_MESSAGE[] = "AAAAABJncnBjLXdlYi10ZXh0LWRhdGE=";
const size_t B64_MESSAGE_SIZE = sizeof(B64_MESSAGE) - 1;
const char B64_MESSAGE_NO_PADDING[] = "AAAAABJncnBjLXdlYi10ZXh0LWRhdGE";
const size_t B64_MESSAGE_NO_PADDING_SIZE = sizeof(B64_MESSAGE_NO_PADDING) - 1;
const char INVALID_B64_MESSAGE[] = "****";
const size_t INVALID_B64_MESSAGE_SIZE = sizeof(INVALID_B64_MESSAGE) - 1;
const char TRAILERS[] = "\x80\x00\x00\x00\x20grpc-status:0\r\ngrpc-message:ok\r\n";
const size_t TRAILERS_SIZE = sizeof(TRAILERS) - 1;

} // namespace

class GrpcWebFilterTest : public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  GrpcWebFilterTest() : grpc_context_(*symbol_table_), filter_(grpc_context_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ~GrpcWebFilterTest() override { filter_.onDestroy(); }

  const std::string& request_content_type() const { return std::get<0>(GetParam()); }

  const std::string& request_accept() const { return std::get<1>(GetParam()); }

  bool isTextRequest() const {
    return request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebText ||
           request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebTextProto;
  }

  bool isBinaryRequest() const {
    return request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWeb ||
           request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebProto;
  }

  bool accept_text_response() const {
    return request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebText ||
           request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebTextProto;
  }

  bool accept_binary_response() const {
    return request_accept() == Http::Headers::get().ContentTypeValues.GrpcWeb ||
           request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebProto;
  }

  bool doStatTracking() const { return filter_.doStatTracking(); }

  void expectErrorResponse(const Http::Code& expected_code, const std::string& expected_message) {
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
        .WillOnce(Invoke([=](Http::ResponseHeaderMap& headers, bool) {
          uint64_t code;
          ASSERT_TRUE(absl::SimpleAtoi(headers.getStatusValue(), &code));
          EXPECT_EQ(static_cast<uint64_t>(expected_code), code);
        }));
    EXPECT_CALL(decoder_callbacks_, encodeData(_, _))
        .WillOnce(Invoke(
            [=](Buffer::Instance& data, bool) { EXPECT_EQ(expected_message, data.toString()); }));
  }

  void expectRequiredGrpcUpstreamHeaders(const Http::TestRequestHeaderMapImpl& request_headers) {
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, request_headers.getContentTypeValue());
    // Ensure we never send content-length upstream
    EXPECT_EQ(nullptr, request_headers.ContentLength());
    EXPECT_EQ(Http::Headers::get().TEValues.Trailers, request_headers.getTEValue());
    EXPECT_EQ(Http::CustomHeaders::get().GrpcAcceptEncodingValues.Default,
              request_headers.get_(Http::CustomHeaders::get().GrpcAcceptEncoding));
  }

  Stats::TestSymbolTable symbol_table_;
  Grpc::ContextImpl grpc_context_;
  GrpcWebFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Http::TestRequestHeaderMapImpl request_headers_{{":path", "/"}};
};

TEST_F(GrpcWebFilterTest, SupportedContentTypes) {
  const std::string supported_content_types[] = {
      Http::Headers::get().ContentTypeValues.GrpcWeb,
      Http::Headers::get().ContentTypeValues.GrpcWebProto,
      Http::Headers::get().ContentTypeValues.GrpcWebText,
      Http::Headers::get().ContentTypeValues.GrpcWebTextProto};
  for (auto& content_type : supported_content_types) {
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    request_headers.addCopy(Http::Headers::get().ContentType, content_type);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
    Http::MetadataMap metadata_map{{"metadata", "metadata"}};
    EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, request_headers.getContentTypeValue());
  }
}

TEST_F(GrpcWebFilterTest, UnsupportedContentType) {
  Buffer::OwnedImpl data;
  request_headers_.addCopy(Http::Headers::get().ContentType, "unsupported");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(GrpcWebFilterTest, NoContentType) {
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(GrpcWebFilterTest, NoPath) {
  Http::TestRequestHeaderMapImpl request_headers{};
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers_));
}

TEST_F(GrpcWebFilterTest, InvalidBase64) {
  request_headers_.addCopy(Http::Headers::get().ContentType,
                           Http::Headers::get().ContentTypeValues.GrpcWebText);
  expectErrorResponse(Http::Code::BadRequest, "Bad gRPC-web request, invalid base64 data.");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  expectRequiredGrpcUpstreamHeaders(request_headers_);

  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl decoded_buffer;
  request_buffer.add(&INVALID_B64_MESSAGE, INVALID_B64_MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_.decodeData(request_buffer, true));
  EXPECT_EQ(decoder_callbacks_.details(), "grpc_base_64_decode_failed");
}

TEST_F(GrpcWebFilterTest, Base64NoPadding) {
  request_headers_.addCopy(Http::Headers::get().ContentType,
                           Http::Headers::get().ContentTypeValues.GrpcWebText);
  expectErrorResponse(Http::Code::BadRequest, "Bad gRPC-web request, invalid base64 data.");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  expectRequiredGrpcUpstreamHeaders(request_headers_);

  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl decoded_buffer;
  request_buffer.add(&B64_MESSAGE_NO_PADDING, B64_MESSAGE_NO_PADDING_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_.decodeData(request_buffer, true));
  EXPECT_EQ(decoder_callbacks_.details(), "grpc_base_64_decode_failed_bad_size");
}

TEST_P(GrpcWebFilterTest, StatsNoCluster) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", request_content_type()},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillOnce(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_FALSE(doStatTracking());
}

TEST_P(GrpcWebFilterTest, StatsNormalResponse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", request_content_type()},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_.encode100ContinueHeaders(continue_headers));

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()
                ->statsScope()
                .counterFromString("grpc-web.lyft.users.BadCompanions.GetBadCompanions.success")
                .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc-web.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_P(GrpcWebFilterTest, StatsErrorResponse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"content-type", request_content_type()},
      {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL,
            decoder_callbacks_.clusterInfo()
                ->statsScope()
                .counterFromString("grpc-web.lyft.users.BadCompanions.GetBadCompanions.failure")
                .value());
  EXPECT_EQ(1UL, decoder_callbacks_.clusterInfo()
                     ->statsScope()
                     .counterFromString("grpc-web.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_P(GrpcWebFilterTest, ExternallyProvidedEncodingHeader) {
  Http::TestRequestHeaderMapImpl request_headers{{"grpc-accept-encoding", "foo"}, {":path", "/"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  EXPECT_EQ("foo", request_headers.get_(Http::CustomHeaders::get().GrpcAcceptEncoding));
}

TEST_P(GrpcWebFilterTest, Unary) {
  // Tests request headers.
  request_headers_.addCopy(Http::Headers::get().ContentType, request_content_type());
  request_headers_.addCopy(Http::CustomHeaders::get().Accept, request_accept());
  request_headers_.addCopy(Http::Headers::get().ContentLength, uint64_t(8));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers_, false));
  expectRequiredGrpcUpstreamHeaders(request_headers_);

  // Tests request data.
  if (isBinaryRequest()) {
    Buffer::OwnedImpl request_buffer;
    Buffer::OwnedImpl decoded_buffer;
    for (size_t i = 0; i < MESSAGE_SIZE; i++) {
      request_buffer.add(&MESSAGE[i], 1);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
      decoded_buffer.move(request_buffer);
    }
    EXPECT_EQ(std::string(MESSAGE, MESSAGE_SIZE), decoded_buffer.toString());
  } else if (isTextRequest()) {
    Buffer::OwnedImpl request_buffer;
    Buffer::OwnedImpl decoded_buffer;
    for (size_t i = 0; i < B64_MESSAGE_SIZE; i++) {
      request_buffer.add(&B64_MESSAGE[i], 1);
      if (i == B64_MESSAGE_SIZE - 1) {
        EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
        decoded_buffer.move(request_buffer);
        break;
      }
      if (i % 4 == 3) {
        EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, false));
      } else {
        EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
                  filter_.decodeData(request_buffer, false));
      }
      decoded_buffer.move(request_buffer);
    }
    EXPECT_EQ(std::string(TEXT_MESSAGE, TEXT_MESSAGE_SIZE), decoded_buffer.toString());
  } else {
    FAIL() << "Unsupported gRPC-Web request content-type: " << request_content_type();
  }

  // Tests request trailers, they are passed through.
  Http::TestRequestTrailerMapImpl request_trailers;
  request_trailers.addCopy(Http::Headers::get().GrpcStatus, "0");
  request_trailers.addCopy(Http::Headers::get().GrpcMessage, "ok");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers_));
  EXPECT_EQ("0", request_trailers.get_("grpc-status"));
  EXPECT_EQ("ok", request_trailers.get_("grpc-message"));

  // Tests response headers.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.addCopy(Http::Headers::get().Status, "200");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("200", response_headers.get_(Http::Headers::get().Status.get()));
  if (accept_binary_response()) {
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWebProto,
              response_headers.getContentTypeValue());
  } else if (accept_text_response()) {
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWebTextProto,
              response_headers.getContentTypeValue());
  } else {
    FAIL() << "Unsupported gRPC-Web request accept: " << request_accept();
  }

  // Tests response data.
  if (accept_binary_response()) {
    Buffer::OwnedImpl response_buffer;
    Buffer::OwnedImpl encoded_buffer;
    for (size_t i = 0; i < MESSAGE_SIZE; i++) {
      response_buffer.add(&MESSAGE[i], 1);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
      encoded_buffer.move(response_buffer);
    }
    EXPECT_EQ(std::string(MESSAGE, MESSAGE_SIZE), encoded_buffer.toString());
  } else if (accept_text_response()) {
    Buffer::OwnedImpl response_buffer;
    Buffer::OwnedImpl encoded_buffer;
    for (size_t i = 0; i < TEXT_MESSAGE_SIZE; i++) {
      response_buffer.add(&TEXT_MESSAGE[i], 1);
      if (i < TEXT_MESSAGE_SIZE - 1) {
        EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
                  filter_.encodeData(response_buffer, false));
      } else {
        EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
      }
      encoded_buffer.move(response_buffer);
    }
    EXPECT_EQ(std::string(B64_MESSAGE, B64_MESSAGE_SIZE), encoded_buffer.toString());
  } else {
    FAIL() << "Unsupported gRPC-Web response content-type: "
           << response_headers.getContentTypeValue();
  }

  // Tests response trailers.
  Buffer::OwnedImpl trailers_buffer;
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) { trailers_buffer.move(data); }));
  Http::TestResponseTrailerMapImpl response_trailers;
  response_trailers.addCopy(Http::Headers::get().GrpcStatus, "0");
  response_trailers.addCopy(Http::Headers::get().GrpcMessage, "ok");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  if (accept_binary_response()) {
    EXPECT_EQ(std::string(TRAILERS, TRAILERS_SIZE), trailers_buffer.toString());
  } else if (accept_text_response()) {
    EXPECT_EQ(std::string(TRAILERS, TRAILERS_SIZE), Base64::decode(trailers_buffer.toString()));
  } else {
    FAIL() << "Unsupported gRPC-Web response content-type: "
           << response_headers.getContentTypeValue();
  }
  EXPECT_EQ(0, response_trailers.size());
}

INSTANTIATE_TEST_SUITE_P(Unary, GrpcWebFilterTest,
                         Combine(Values(Http::Headers::get().ContentTypeValues.GrpcWeb,
                                        Http::Headers::get().ContentTypeValues.GrpcWebProto,
                                        Http::Headers::get().ContentTypeValues.GrpcWebText,
                                        Http::Headers::get().ContentTypeValues.GrpcWebTextProto),
                                 Values(Http::Headers::get().ContentTypeValues.GrpcWeb,
                                        Http::Headers::get().ContentTypeValues.GrpcWebProto,
                                        Http::Headers::get().ContentTypeValues.GrpcWebText,
                                        Http::Headers::get().ContentTypeValues.GrpcWebTextProto)));

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
