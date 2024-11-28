#include <google/protobuf/extension_set.h>

#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"

#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"

#include "test/mocks/server/factory_context.h"
#include "test/proto/apikeys.pb.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "google/api/httpbody.pb.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

namespace {

class GrpcJsonReverseTranscoderFilterTest : public testing::Test {
protected:
  GrpcJsonReverseTranscoderFilterTest()
      : api_(Api::createApiForTest()),
        config_(std::make_shared<GrpcJsonReverseTranscoderConfig>(bookstoreProtoConfig(), *api_)),
        filter_(config_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);

    // Set buffer limit same as Envoy's default (1 MiB)
    ON_CALL(decoder_callbacks_, decoderBufferLimit()).WillByDefault(Return(2 << 20));
    ON_CALL(encoder_callbacks_, encoderBufferLimit()).WillByDefault(Return(2 << 20));

    ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));
  }

  static envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
      GrpcJsonReverseTranscoder
      bookstoreProtoConfig(bool include_version_header = false, bool set_body_size = false) {
    std::string json_string = "{\"descriptor_path\": \"" + bookstoreDescriptorPath() + "\"";
    if (set_body_size) {
      json_string += ",\"max_request_body_size\":2222,"
                     "\"max_response_body_size\":2222";
    }
    if (include_version_header) {
      json_string += ",\"api_version_header\": \"Api-Version\"";
    }
    json_string += "}";
    return makeProtoConfig(json_string);
  }

  static envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
      GrpcJsonReverseTranscoder
      makeProtoConfig(const std::string json_string) {
    envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
        proto_config;
    TestUtility::loadFromJson(json_string, proto_config);
    return proto_config;
  }

  static const std::string bookstoreDescriptorPath() {
    return TestEnvironment::runfilesPath("test/proto/bookstore.descriptor");
  }

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<GrpcJsonReverseTranscoderConfig> config_;
  GrpcJsonReverseTranscoderFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

// Test the header encoding.
TEST_F(GrpcJsonReverseTranscoderFilterTest, GrpcRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/BulkCreateShelf"},
                                         {"content-type", "application/grpc"}};
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));
  EXPECT_TRUE(filter_.shouldTranscodeResponse());
  EXPECT_EQ(headers.getContentTypeValue(), Http::Headers::get().ContentTypeValues.Json);
  EXPECT_EQ(headers.getMethodValue(), Http::Headers::get().MethodValues.Post);
}

// Test the header encoding for unknown gRPC service.
TEST_F(GrpcJsonReverseTranscoderFilterTest, GrpcRequestUnknowService) {
  Http::TestRequestHeaderMapImpl headers{{"content-type", "application/grpc"},
                                         {":method", "POST"},
                                         {":path", "/grpc.service/UnknownGrpcMethod"}};
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test the header encoding for the non-gRPC service.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NonGrpcRequest) {
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, false));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test the pass through of data for request with no transcoder instance.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NoTranscoding) {
  Buffer::OwnedImpl request;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request, true));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test the request transcoding for a gRPC request.
TEST_F(GrpcJsonReverseTranscoderFilterTest, TranscodeBody) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/UpdateBook"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  bookstore::UpdateBookRequest request;
  request.set_shelf(12345);
  request.mutable_book()->set_id(123);
  request.mutable_book()->set_title("Kids book");
  request.mutable_book()->set_author("John Doe");
  std::string expected_request = "{\"author\":\"John Doe\",\"id\":\"123\",\"title\":\"Kids book\"}";
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(*request_data, true));
  EXPECT_EQ(headers.getPathValue(), "/shelves/12345/books/123");
  EXPECT_EQ(request_data->toString(), expected_request);
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(expected_request.size()));
}

// Test the request transcoding of the request where some of the fields
// from the gRPC message will be added to the path as query params.
TEST_F(GrpcJsonReverseTranscoderFilterTest, GrpcRequestWithQueryParams) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/ListBooks"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));
  bookstore::ListBooksRequest request;
  request.set_shelf(12345);
  request.set_author(567);
  request.set_theme("Science Fiction");
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(*request_data, true));
  EXPECT_EQ(headers.getPathValue(), "/shelves/12345/books?author=567&theme=Science%20Fiction");
}

// Test the transcoding of the request when whole gRPC message will be sent as a
// request payload.
TEST_F(GrpcJsonReverseTranscoderFilterTest, WholeRequestAsPayload) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/CreateShelfBodyWildcard"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  bookstore::CreateShelfRequest request;
  request.mutable_shelf()->set_id(123);
  request.mutable_shelf()->set_theme("Kids");
  std::string expected_request = "{\"shelf\":{\"id\":\"123\",\"theme\":\"Kids\"}}";
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(*request_data, true));
  EXPECT_EQ(headers.getPathValue(), "/shelf/123");
  EXPECT_EQ(request_data->toString(), expected_request);
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(expected_request.size()));
}

// Test request transcoding with gRPC message with missing the path placeholders.
TEST_F(GrpcJsonReverseTranscoderFilterTest, RequestMissingPathParam) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/CreateShelfBodyWildcard"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  bookstore::CreateShelfRequest request;
  request.mutable_shelf()->set_theme("Kids");
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(*request_data, true));
}

// Test request transcoding with gRPC message missing the payload for the HTTP request.
TEST_F(GrpcJsonReverseTranscoderFilterTest, TranscodeMissingBody) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/UpdateBook"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));

  bookstore::UpdateBookRequest request;
  request.set_shelf(12345);
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(*request_data, true));
}

// Test request transcoding with data decoding divided into multiple frames.
TEST_F(GrpcJsonReverseTranscoderFilterTest, RequestWithMultipleDataFrames) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/UpdateBook"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  bookstore::UpdateBookRequest request;
  request.set_shelf(12345);
  request.mutable_book()->set_id(123);
  request.mutable_book()->set_title("Kids book");
  request.mutable_book()->set_author("John Doe");
  std::string expected_request = "{\"author\":\"John Doe\",\"id\":\"123\",\"title\":\"Kids book\"}";
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_.decodeData(*request_data, false));
  request_data->add("");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(*request_data, true));
  EXPECT_EQ(headers.getPathValue(), "/shelves/12345/books/123");
  EXPECT_EQ(request_data->toString(), expected_request);
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(expected_request.size()));
}

// Test request transcoding where request type is google.api.HttpBody
TEST_F(GrpcJsonReverseTranscoderFilterTest, TranscodeHttpBody) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/EchoRawBody"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  google::api::HttpBody http_body;
  http_body.set_content_type("application/custom");
  http_body.set_data("{\"title\":\"Alchemist\"}");
  auto data_frame = Grpc::Common::serializeToGrpcFrame(http_body);
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data, true));
  data.add(*data_frame);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(data, true));
  EXPECT_EQ(data.toString(), "{\"title\":\"Alchemist\"}");
  EXPECT_EQ(headers.getContentTypeValue(), "application/custom");
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(http_body.data().size()));
}

// Test request transcoding where request type is google.api.HttpBody and
// the path contains placeholders.
TEST_F(GrpcJsonReverseTranscoderFilterTest, MissingPlaceholderValue) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"},
      {":path", "/bookstore.ServiceWithPathPlaceholderHttpBody/EchoRawBodyError"},
      {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));

  google::api::HttpBody http_body;
  http_body.set_content_type("application/custom");
  http_body.set_data("{\"title\":\"Alchemist\"}");
  auto request_data = Grpc::Common::serializeToGrpcFrame(http_body);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(*request_data, true));
}

// Test request transcoding for the payload is a nested filed of type google.api.HttpBody.
TEST_F(GrpcJsonReverseTranscoderFilterTest, TranscodeNestedHttpBody) {
  auto config =
      std::make_shared<GrpcJsonReverseTranscoderConfig>(bookstoreProtoConfig(true), *api_);
  auto filter = GrpcJsonReverseTranscoderFilter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);
  filter.setEncoderFilterCallbacks(encoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/CreateBookHttpBody"},
                                         {"content-type", "application/grpc"},
                                         {"api-version", "v1alpha1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter.decodeHeaders(headers, false));

  bookstore::CreateBookHttpBodyRequest request;
  request.set_shelf(12345);
  request.mutable_book()->set_content_type("application/custom");
  request.mutable_book()->set_data("{\"title\":\"Alchemist\"}");
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter.decodeData(*request_data, true));
  EXPECT_EQ(request_data->toString(), "{\"title\":\"Alchemist\"}");
  EXPECT_EQ(headers.getContentTypeValue(), "application/custom");
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(request.book().data().size()));
}

// Test transcoding with buffer size set to the value larger than the default limit.
TEST_F(GrpcJsonReverseTranscoderFilterTest, TranscodeWithBufferExpansion) {
  auto config =
      std::make_shared<GrpcJsonReverseTranscoderConfig>(bookstoreProtoConfig(true, true), *api_);
  auto filter = GrpcJsonReverseTranscoderFilter(config);
  filter.setDecoderFilterCallbacks(decoder_callbacks_);
  filter.setEncoderFilterCallbacks(encoder_callbacks_);

  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillRepeatedly(Return(2));
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit()).WillRepeatedly(Return(2));

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/CreateBookHttpBody"},
                                         {"content-type", "application/grpc"},
                                         {"api-version", "v1alpha1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter.decodeHeaders(headers, false));

  bookstore::CreateBookHttpBodyRequest request;
  request.set_shelf(12345);
  request.mutable_book()->set_content_type("application/custom");
  request.mutable_book()->set_data("{\"title\":\"Alchemist\"}");
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter.decodeData(*request_data, true));
  EXPECT_EQ(request_data->toString(), "{\"title\":\"Alchemist\"}");
  EXPECT_EQ(headers.getContentTypeValue(), "application/custom");
  EXPECT_EQ(headers.getContentLengthValue(), std::to_string(request.book().data().size()));
}

// Test transcoding with request overflowing the buffer limits.
TEST_F(GrpcJsonReverseTranscoderFilterTest, DecoderBufferLimitOverflow) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/DeleteBook"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  bookstore::DeleteBookRequest request;
  request.set_shelf(12345);
  request.set_book(6789);
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillOnce(Return(3));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(*request_data, true));
}

// Test transcoding with request of type google.api.HttpBody overflowing the buffer limits.
TEST_F(GrpcJsonReverseTranscoderFilterTest, DecoderBufferLimitOverflowHttpBody) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/CreateBookHttpBody"},
                                         {"content-type", "application/grpc"},
                                         {"api-version", "v1alpha1"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  bookstore::DeleteBookRequest request;
  request.set_shelf(12345);
  request.set_book(6789);
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);
  EXPECT_CALL(decoder_callbacks_, decoderBufferLimit()).WillOnce(Return(3));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(*request_data, true));
}

// Test transcoding where the transcoded request message is not a valid JSON object.
TEST_F(GrpcJsonReverseTranscoderFilterTest, RequestParsingFailure) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {":path", "/bookstore.Bookstore/DeleteBook"},
                                         {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(buffer, true));
}

// Test header encoding for a gRPC response.
TEST_F(GrpcJsonReverseTranscoderFilterTest, GrpcResponse) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-type", "application/grpc"},
                                          {"grpc-status", "0"},
                                          {"grpc-message", "OK"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(headers, false));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test header encoding for a non-gRPC response.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NonGrpcResponse) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(headers, false));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test data encoding for the case where response is passed through.
TEST_F(GrpcJsonReverseTranscoderFilterTest, PassThroughResponseData) {
  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(buffer, true));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test trailer encoding for the case where response is passed through.
TEST_F(GrpcJsonReverseTranscoderFilterTest, PassThroughResponseTrailer) {
  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(trailers));
  EXPECT_FALSE(filter_.shouldTranscodeResponse());
}

// Test transcoding of non-200 responses return just the headers.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NonOKResponseStatusOnly) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));
  Http::TestResponseHeaderMapImpl res_headers{{":status", "404"},
                                              {"content-type", "application/json"}};
  Buffer::OwnedImpl expected_response;
  Grpc::Encoder().prependFrameHeader(Grpc::GRPC_FH_DEFAULT, expected_response);
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
      .WillOnce(Invoke([&expected_response](Buffer::Instance& data, bool) {
        EXPECT_EQ(data.toString(), expected_response.toString());
      }));
  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, true));
  EXPECT_EQ(res_headers.getContentTypeValue(), Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(trailers.getGrpcStatusValue(), "12"); // 12 is Unimplemented
  EXPECT_EQ(res_headers.getStatusValue(), "200");
}

// Test transcoding of non-200 response returning headers and body.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NonOKResponse) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

  Http::TestResponseHeaderMapImpl res_headers{{":status", "404"},
                                              {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));
  EXPECT_EQ(res_headers.getContentTypeValue(), Http::Headers::get().ContentTypeValues.Grpc);
  EXPECT_EQ(res_headers.getStatusValue(), "200");

  Buffer::OwnedImpl buffer;
  buffer.add("NOT FOUND");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(buffer, true));
  EXPECT_EQ(trailers.getGrpcStatusValue(), "12"); // 12 is Unimplemented
  EXPECT_EQ(trailers.getGrpcMessageValue(), "NOT FOUND");
}

// Test transcoding of non-200 response returning headers, body and trailers.
TEST_F(GrpcJsonReverseTranscoderFilterTest, NonOKResponseWithTrailer) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _)).Times(0);
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).Times(0);

  Http::TestResponseHeaderMapImpl res_headers{{":status", "404"},
                                              {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));

  Buffer::OwnedImpl buffer;
  buffer.add("NOT FOUND");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(buffer, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(trailers));
  EXPECT_EQ(trailers.getGrpcStatusValue(), "12"); // 12 is Unimplemented
  EXPECT_EQ(trailers.getGrpcMessageValue(), "NOT FOUND");
}

// Test transcoding of a JSON response.
TEST_F(GrpcJsonReverseTranscoderFilterTest, OKResponse) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  std::string book_str = "{\"id\":123,\"author\":\"John Doe\",\"title\":\"A Book\"}";
  Buffer::OwnedImpl buffer;
  buffer.add(book_str);

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

  Http::TestResponseHeaderMapImpl res_headers{{":status", "200"},
                                              {"content-type", "application/json"},
                                              {"content-length", std::to_string(buffer.length())}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(buffer, true));
  bookstore::Book book;
  book.set_id(123);
  book.set_author("John Doe");
  book.set_title("A Book");
  auto book_buffer = Grpc::Common::serializeToGrpcFrame(book);
  EXPECT_EQ(buffer.toString(), book_buffer.get()->toString());
  EXPECT_EQ(trailers.getGrpcStatusValue(), "0"); // OK
}

// Test transcoding of a JSON response with trailers.
TEST_F(GrpcJsonReverseTranscoderFilterTest, OKResponseWithTrailer) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  std::string book_str = "{\"id\":123,\"author\":\"John Doe\",\"title\":\"A Book\"}";
  Buffer::OwnedImpl buffer;
  buffer.add(book_str);

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool) {
        bookstore::Book book;
        book.set_id(123);
        book.set_author("John Doe");
        book.set_title("A Book");
        auto book_buffer = Grpc::Common::serializeToGrpcFrame(book);
        EXPECT_EQ(data.toString(), book_buffer.get()->toString());
      }));

  Http::TestResponseHeaderMapImpl res_headers{{":status", "200"},
                                              {"content-type", "application/json"},
                                              {"content-length", std::to_string(buffer.length())}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(buffer, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(trailers));
  EXPECT_EQ(trailers.getGrpcStatusValue(), "0");
}

// Test transcoding of a response that's supposed to be transcoded into google.api.HttpBody
TEST_F(GrpcJsonReverseTranscoderFilterTest, OKHttpBodyResponse) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/GetIndex"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

  std::string book_str = "{\"id\":123,\"author\":\"John Doe\",\"title\":\"A Book\"}";
  Buffer::OwnedImpl buffer;
  buffer.add(book_str);

  Http::TestResponseHeaderMapImpl res_headers{{":status", "200"},
                                              {"content-type", "application/json"},
                                              {"content-length", std::to_string(buffer.length())}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(buffer, true));

  google::api::HttpBody body;
  body.set_content_type("application/json");
  body.set_data(book_str);
  auto body_buffer = Grpc::Common::serializeToGrpcFrame(body);
  EXPECT_EQ(buffer.toString(), body_buffer.get()->toString());
  EXPECT_EQ(trailers.getGrpcStatusValue(), "0");
}

// Test transcoding of an empty response that's supposed to be transcoded into google.api.HttpBody
TEST_F(GrpcJsonReverseTranscoderFilterTest, EmptyHttpBodyResponse) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/GetIndex"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));

  Buffer::OwnedImpl buffer;

  Http::TestResponseHeaderMapImpl res_headers{{":status", "200"},
                                              {"content-type", "application/json"},
                                              {"content-length", std::to_string(buffer.length())}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(buffer, true));

  EXPECT_EQ(trailers.getGrpcStatusValue(), "0");
}

// Test transcoding of a response that's supposed to be transcoded into google.api.HttpBody with
// trailers.
TEST_F(GrpcJsonReverseTranscoderFilterTest, OKHttpBodyResponseWithTrailer) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/GetIndex"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  std::string book_str = "{\"id\":123,\"author\":\"John Doe\",\"title\":\"A Book\"}";
  Buffer::OwnedImpl buffer;
  buffer.add(book_str);

  EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
      .WillOnce(Invoke([&book_str](Buffer::Instance& data, bool) {
        google::api::HttpBody body;
        body.set_content_type("application/json");
        body.set_data(book_str);
        auto body_buffer = Grpc::Common::serializeToGrpcFrame(body);
        EXPECT_EQ(data.toString(), body_buffer.get()->toString());
      }));

  Http::TestResponseHeaderMapImpl res_headers{{":status", "200"},
                                              {"content-type", "application/json"},
                                              {"content-length", std::to_string(buffer.length())}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(res_headers, false));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(buffer, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(trailers));
  EXPECT_EQ(trailers.getGrpcStatusValue(), "0");
}

// Test the overflow of buffer limit in the encoder path.
TEST_F(GrpcJsonReverseTranscoderFilterTest, EncoderBufferLimitOverflow) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Buffer::OwnedImpl response;
  response.add("This is a sample response");
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit).WillOnce(Return(3));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(response, false));
}

// Test the overflow of buffer limit for the google.api.HttpBody reponse.
TEST_F(GrpcJsonReverseTranscoderFilterTest, EncoderBufferLimitOverflowHttpBody) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/GetIndex"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Buffer::OwnedImpl response;
  response.add("This is a sample response");
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit).WillOnce(Return(3));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(response, false));
}

// Test the transcoding of an invalid response.
TEST_F(GrpcJsonReverseTranscoderFilterTest, ResponseTranscodingFailure) {
  Http::TestRequestHeaderMapImpl req_headers{{":method", "POST"},
                                             {":path", "/bookstore.Bookstore/UpdateBook"},
                                             {"content-type", "application/grpc"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_.decodeHeaders(req_headers, false));

  Buffer::OwnedImpl response;
  response.add("This is a sample response");
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.encodeData(response, false));
}

// Test metadata and 1xxHeaders encoding and trailer decoding.
TEST_F(GrpcJsonReverseTranscoderFilterTest, MiscEncodingAndDecoding) {
  Http::TestRequestTrailerMapImpl decode_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(decode_trailers));
  Http::TestResponseHeaderMapImpl continue_headers{{":status", "000"}};
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_.encode1xxHeaders(continue_headers));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.encodeMetadata(metadata_map));
}

// Test parsing of a proto descriptor in binary format.
TEST_F(GrpcJsonReverseTranscoderFilterTest, ParseBinaryConfig) {
  envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
      config;
  config.set_descriptor_binary(api_->fileSystem().fileReadToEnd(bookstoreDescriptorPath()).value());
  EXPECT_NO_THROW(GrpcJsonReverseTranscoderConfig(config, *api_));
}

// Test parsing of an invalid proto descriptor binary.
TEST_F(GrpcJsonReverseTranscoderFilterTest, ParseInvalidBinaryConfig) {
  envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
      config;
  config.set_descriptor_binary("Invalid Config");
  EXPECT_THROW_WITH_MESSAGE(GrpcJsonReverseTranscoderConfig(config, *api_), EnvoyException,
                            "Unable to parse proto descriptor binary");
}

// Test parsing a config with the proto descriptor.
TEST_F(GrpcJsonReverseTranscoderFilterTest, ConfigWithoutDescriptor) {
  envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
      config;
  EXPECT_THROW_WITH_MESSAGE(GrpcJsonReverseTranscoderConfig(config, *api_), EnvoyException,
                            "Descriptor set not set");
}

// Test transcoder creation.
TEST_F(GrpcJsonReverseTranscoderFilterTest, CreateTranscoder) {
  auto config = GrpcJsonReverseTranscoderConfig(bookstoreProtoConfig(), *api_);

  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder1;
  TranscoderInputStreamImpl request_in1, response_in1;
  HttpRequestParams request_params1;
  MethodInfo method_info1;
  absl::Status status =
      config.CreateTranscoder("/bookstore.Bookstore/CreateBook", request_in1, response_in1,
                              transcoder1, request_params1, method_info1);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder1);
  EXPECT_EQ(request_params1.method, Http::Headers::get().MethodValues.Put);
  EXPECT_EQ(request_params1.http_body_field, "book");
  EXPECT_EQ(request_params1.http_rule_path, "/shelves/{shelf}/books");

  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder2;
  TranscoderInputStreamImpl request_in2, response_in2;
  HttpRequestParams request_params2;
  MethodInfo method_info2;
  status = config.CreateTranscoder("/bookstore.Bookstore/BookstoreOptions", request_in2,
                                   response_in2, transcoder2, request_params2, method_info2);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(transcoder2);
  EXPECT_EQ(request_params2.method, Http::Headers::get().MethodValues.Options);
  EXPECT_TRUE(request_params2.http_body_field.empty());
  EXPECT_EQ(request_params2.http_rule_path, "/shelves/{shelf}");
}

// Test creation of a transcoder instance for a gRPC method that doesn't
// exist in the proto descriptor.
TEST_F(GrpcJsonReverseTranscoderFilterTest, CreateTranscoderInvalidMethod) {
  auto config = GrpcJsonReverseTranscoderConfig(bookstoreProtoConfig(), *api_);

  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_, response_in_;
  HttpRequestParams request_params;
  MethodInfo method_info;
  absl::Status status =
      config.CreateTranscoder("/bookstore.Bookstore/InvalidMethod", request_in_, response_in_,
                              transcoder_, request_params, method_info);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

// Test creation of a transcoder instance for a gRPC method without google.api.http annotation.
TEST_F(GrpcJsonReverseTranscoderFilterTest, CreateTranscoderWithoutHttpAnnotations) {
  auto config = GrpcJsonReverseTranscoderConfig(bookstoreProtoConfig(), *api_);

  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_, response_in_;
  HttpRequestParams request_params;
  MethodInfo method_info;
  absl::Status status =
      config.CreateTranscoder("/bookstore.Bookstore/GetBook", request_in_, response_in_,
                              transcoder_, request_params, method_info);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

} // namespace
} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
