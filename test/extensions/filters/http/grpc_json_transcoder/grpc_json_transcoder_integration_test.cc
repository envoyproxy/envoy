#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

using Envoy::Protobuf::TextFormat;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::Status;
using Envoy::ProtobufUtil::error::Code;
using Envoy::ProtobufWkt::Empty;

namespace Envoy {
namespace {

// A magic header value which marks header as not expected.
constexpr char UnexpectedHeaderValue[] = "Unexpected header value";

class GrpcJsonTranscoderIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GrpcJsonTranscoderIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    const std::string filter =
        R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.http.transcoder.v2.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
            )EOF";
    config_helper_.addFilter(
        fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  }

  /**
   * Global destructor for all integration tests.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

protected:
  template <class RequestType, class ResponseType>
  void testTranscoding(Http::RequestHeaderMap&& request_headers, const std::string& request_body,
                       const std::vector<std::string>& grpc_request_messages,
                       const std::vector<std::string>& grpc_response_messages,
                       const Status& grpc_status, Http::HeaderMap&& response_headers,
                       const std::string& response_body, bool full_response = true,
                       bool always_send_trailers = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    IntegrationStreamDecoderPtr response;
    if (!request_body.empty()) {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      Buffer::OwnedImpl body(request_body);
      codec_client_->sendData(*request_encoder_, body, true);
    } else {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    }

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    if (!grpc_request_messages.empty()) {
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

      std::string dump;
      for (char ch : upstream_request_->body().toString()) {
        dump += std::to_string(int(ch));
        dump += " ";
      }

      Grpc::Decoder grpc_decoder;
      std::vector<Grpc::Frame> frames;
      EXPECT_TRUE(grpc_decoder.decode(upstream_request_->body(), frames)) << dump;
      EXPECT_EQ(grpc_request_messages.size(), frames.size());

      for (size_t i = 0; i < grpc_request_messages.size(); ++i) {
        RequestType actual_message;
        if (frames[i].length_ > 0) {
          EXPECT_TRUE(actual_message.ParseFromString(frames[i].data_->toString()));
        }
        RequestType expected_message;
        EXPECT_TRUE(TextFormat::ParseFromString(grpc_request_messages[i], &expected_message));
        EXPECT_THAT(actual_message, ProtoEq(expected_message));
      }

      Http::TestResponseHeaderMapImpl response_headers;
      response_headers.setStatus(200);
      response_headers.setContentType("application/grpc");
      if (grpc_response_messages.empty() && !always_send_trailers) {
        response_headers.setGrpcStatus(static_cast<uint64_t>(grpc_status.error_code()));
        response_headers.setGrpcMessage(absl::string_view(grpc_status.error_message().data(),
                                                          grpc_status.error_message().size()));
        upstream_request_->encodeHeaders(response_headers, true);
      } else {
        response_headers.addCopy(Http::LowerCaseString("trailer"), "Grpc-Status");
        response_headers.addCopy(Http::LowerCaseString("trailer"), "Grpc-Message");
        upstream_request_->encodeHeaders(response_headers, false);
        for (const auto& response_message_str : grpc_response_messages) {
          ResponseType response_message;
          EXPECT_TRUE(TextFormat::ParseFromString(response_message_str, &response_message));
          auto buffer = Grpc::Common::serializeToGrpcFrame(response_message);
          upstream_request_->encodeData(*buffer, false);
        }
        Http::TestResponseTrailerMapImpl response_trailers;
        response_trailers.setGrpcStatus(static_cast<uint64_t>(grpc_status.error_code()));
        response_trailers.setGrpcMessage(absl::string_view(grpc_status.error_message().data(),
                                                           grpc_status.error_message().size()));
        upstream_request_->encodeTrailers(response_trailers);
      }
      EXPECT_TRUE(upstream_request_->complete());
    }

    response->waitForEndStream();
    EXPECT_TRUE(response->complete());

    if (response->headers().get(Http::LowerCaseString("transfer-encoding")) == nullptr ||
        !absl::StartsWith(response->headers()
                              .get(Http::LowerCaseString("transfer-encoding"))
                              ->value()
                              .getStringView(),
                          "chunked")) {
      EXPECT_EQ(response->headers().get(Http::LowerCaseString("trailer")), nullptr);
    }

    response_headers.iterate(
        [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
          auto* response = static_cast<IntegrationStreamDecoder*>(context);
          Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
          if (entry.value() == UnexpectedHeaderValue) {
            EXPECT_FALSE(response->headers().get(lower_key));
          } else {
            EXPECT_EQ(entry.value().getStringView(),
                      response->headers().get(lower_key)->value().getStringView());
          }
          return Http::HeaderMap::Iterate::Continue;
        },
        response.get());
    if (!response_body.empty()) {
      if (full_response) {
        EXPECT_EQ(response_body, response->body());
      } else {
        EXPECT_TRUE(absl::StartsWith(response->body(), response_body));
      }
    }

    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcJsonTranscoderIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPost) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "30"},
                                      {"grpc-status", "0"}},
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, QueryParams) {
  HttpIntegrationTest::initialize();
  // 1. Binding theme='Children' in CreateShelfRequest
  // Using the following HTTP template:
  //   POST /shelves
  //   body: shelf
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.theme=Children"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { theme: "Children" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"20","theme":"Children"})");

  // 2. Binding theme='Children' and id='999' in CreateShelfRequest
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.id=999&shelf.theme=Children"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { id: 999 theme: "Children" })"}, {R"(id: 999 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"999","theme":"Children"})");

  // 3. Binding shelf=1, book=<post body> and book.title='War and Peace' in CreateBookRequest
  //    Using the following HTTP template:
  //      POST /shelves/{shelf}/books
  //      body: book
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                     {":path", "/shelves/1/books?book.title=War%20and%20Peace"},
                                     {":authority", "host"}},
      R"({"author" : "Leo Tolstoy"})",
      {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War and Peace"})");

  // 4. Binding shelf=1, book.author='Leo Tolstoy' and book.title='War and Peace' in
  // CreateBookRequest
  //    Using the following HTTP template:
  //      POST /shelves/{shelf}/books
  //      body: book
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PUT"},
          {":path", "/shelves/1/books?book.author=Leo%20Tolstoy&book.title=War%20and%20Peace"},
          {":authority", "host"}},
      "", {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War and Peace"})");

  // 5. Test URL decoding.
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                     {":path", "/shelves/1/books?book.title=War%20%26%20Peace"},
                                     {":authority", "host"}},
      R"({"author" : "Leo Tolstoy"})",
      {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War & Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War & Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War & Peace"})");

  // 6. Binding all book fields through query params.
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PUT"},
          {":path",
           "/shelves/1/books?book.id=999&book.author=Leo%20Tolstoy&book.title=War%20and%20Peace"},
          {":authority", "host"}},
      "", {R"(shelf: 1 book { id : 999  author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 999 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"999","author":"Leo Tolstoy","title":"War and Peace"})");

  // 7. Binding shelf=3, book=<post body> and the repeated field book.quote with
  //     two values ("Winter is coming" and "Hold the door") in CreateBookRequest.
  //     These values should be added to the repeated field in addition to what is
  //     translated in the body.
  //     Using the following HTTP template:
  //       POST /shelves/{shelf}/books
  //       body: book
  std::string reqBody =
      R"({"id":"999","author":"George R.R. Martin","title":"A Game of Thrones",)"
      R"("quotes":["A girl has no name","A very small man can cast a very large shadow"]})";
  std::string grpcResp = R"(id : 999  author: "George R.R. Martin" title: "A Game of Thrones"
      quotes: "A girl has no name" quotes : "A very small man can cast a very large shadow"
      quotes: "Winter is coming" quotes : "Hold the door")";
  std::string expectGrpcRequest = absl::StrCat("shelf: 1 book {", grpcResp, "}");
  std::string respBody =
      R"({"id":"999","author":"George R.R. Martin","title":"A Game of Thrones","quotes":["A girl has no name")"
      R"(,"A very small man can cast a very large shadow","Winter is coming","Hold the door"]})";

  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PUT"},
          {":path",
           "/shelves/1/books?book.quotes=Winter%20is%20coming&book.quotes=Hold%20the%20door"},
          {":authority", "host"}},
      reqBody, {expectGrpcRequest}, {grpcResp}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      respBody);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGet) {
  HttpIntegrationTest::initialize();
  testTranscoding<Empty, bookstore::ListShelvesResponse>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves"}, {":authority", "host"}},
      "", {""}, {R"(shelves { id: 20 theme: "Children" }
          shelves { id: 1 theme: "Foo" } )"},
      Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "69"},
                                      {"grpc-status", "0"}},
      R"({"shelves":[{"id":"20","theme":"Children"},{"id":"1","theme":"Foo"}]})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetHttpBody) {
  HttpIntegrationTest::initialize();
  testTranscoding<Empty, google::api::HttpBody>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/index"}, {":authority", "host"}},
      "", {""}, {R"(content_type: "text/html" data: "<h1>Hello!</h1>" )"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "text/html"},
                                      {"content-length", "15"},
                                      {"grpc-status", "0"}},
      R"(<h1>Hello!</h1>)");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryEchoHttpBody) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::EchoBodyRequest, google::api::HttpBody>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/echoBody?arg=oops"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}},
      "Hello!", {R"(arg: "oops" nested { content { content_type: "text/plain" data: "Hello!" } })"},
      {R"(content_type: "text/html" data: "<h1>Hello!</h1>" )"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "text/html"},
                                      {"content-length", "15"},
                                      {"grpc-status", "0"}},
      R"(<h1>Hello!</h1>)");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetError) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100?"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(Code::NOT_FOUND, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 100 Not Found"}},
      "");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetError1) {
  const std::string filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.http.transcoder.v2.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              ignore_unknown_query_parameters : true
            )EOF";
  config_helper_.addFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1&shelf=9999"},
                                     {":authority", "host"}},
      "", {"shelf: 9999"}, {}, Status(Code::NOT_FOUND, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "");
}

// Test an upstream that returns an error in a trailer-only response.
TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryErrorConvertedToJson) {
  const std::string filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.http.transcoder.v2.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.addFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(Code::NOT_FOUND, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"},
                                      {"content-type", "application/json"},
                                      {"grpc-status", UnexpectedHeaderValue},
                                      {"grpc-message", UnexpectedHeaderValue}},
      R"({"code":5,"message":"Shelf 100 Not Found"})");
}

// Upstream sends headers (e.g. sends metadata), and then sends trailer with an error.
TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryErrorInTrailerConvertedToJson) {
  const std::string filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.http.transcoder.v2.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.addFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(Code::NOT_FOUND, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"},
                                      {"content-type", "application/json"},
                                      {"grpc-status", UnexpectedHeaderValue},
                                      {"grpc-message", UnexpectedHeaderValue}},
      R"({"code":5,"message":"Shelf 100 Not Found"})", true, true);
}

// Streaming backend returns an error in a trailer-only response.
TEST_P(GrpcJsonTranscoderIntegrationTest, StreamingErrorConvertedToJson) {
  const std::string filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.config.filter.http.transcoder.v2.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.addFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::ListBooksRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/37/books"}, {":authority", "host"}},
      "", {"shelf: 37"}, {}, Status(Code::NOT_FOUND, "Shelf 37 Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"},
                                      {"content-type", "application/json"},
                                      {"grpc-status", UnexpectedHeaderValue},
                                      {"grpc-message", UnexpectedHeaderValue}},
      R"({"code":5,"message":"Shelf 37 Not Found"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryDelete) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::DeleteBookRequest, Empty>(
      Http::TestRequestHeaderMapImpl{
          {":method", "DELETE"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      "", {"shelf: 456 book: 123"}, {""}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "2"},
                                      {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPatch) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::UpdateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PATCH"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 456 book { id: 123 author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 123 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "59"},
                                      {"grpc-status", "0"}},
      R"({"id":"123","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryCustom) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, Empty>(
      Http::TestRequestHeaderMapImpl{
          {":method", "OPTIONS"}, {":path", "/shelves/456"}, {":authority", "host"}},
      "", {"shelf: 456"}, {""}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "2"},
                                      {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, BindingAndBody) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PUT"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingGet) {
  HttpIntegrationTest::initialize();

  // 1: Normal streaming get
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"},
      {R"(id: 1 author: "Neal Stephenson" title: "Readme")",
       R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
      Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"([{"id":"1","author":"Neal Stephenson","title":"Readme"})"
      R"(,{"id":"2","author":"George R.R. Martin","title":"A Game of Thrones"}])");

  // 2: Empty response (trailers only) from streaming backend.
  // Response type is a valid JSON, so content type should be application/json.
  // Regression test for github.com/envoyproxy/envoy#5011
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/2/books"}, {":authority", "host"}},
      "", {"shelf: 2"}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      "[]");

  // 3: Empty response (trailers only) from streaming backend, with a gRPC error.
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/37/books"}, {":authority", "host"}},
      "", {"shelf: 37"}, {}, Status(Code::NOT_FOUND, "Shelf 37 not found"),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      "[]");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamingPost) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/bulk/shelves"}, {":authority", "host"}},
      R"([
        { "theme" : "Classics" },
        { "theme" : "Satire" },
        { "theme" : "Russian" },
        { "theme" : "Children" },
        { "theme" : "Documentary" },
        { "theme" : "Mystery" },
      ])",
      {R"(shelf { theme: "Classics" })", R"(shelf { theme: "Satire" })",
       R"(shelf { theme: "Russian" })", R"(shelf { theme: "Children" })",
       R"(shelf { theme: "Documentary" })", R"(shelf { theme: "Mystery" })"},
      {R"(id: 3 theme: "Classics")", R"(id: 4 theme: "Satire")", R"(id: 5 theme: "Russian")",
       R"(id: 6 theme: "Children")", R"(id: 7 theme: "Documentary")", R"(id: 8 theme: "Mystery")"},
      Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"transfer-encoding", "chunked"}},
      R"([{"id":"3","theme":"Classics"})"
      R"(,{"id":"4","theme":"Satire"})"
      R"(,{"id":"5","theme":"Russian"})"
      R"(,{"id":"6","theme":"Children"})"
      R"(,{"id":"7","theme":"Documentary"})"
      R"(,{"id":"8","theme":"Mystery"}])");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, InvalidJson) {
  HttpIntegrationTest::initialize();
  // Usually the response would be
  // "Unexpected token.\n"
  //    "INVALID_JSON\n"
  //    "^"
  // If Envoy does a short read of the upstream connection, it may only read part of the
  // string "INVALID_JSON". Envoy will note "Unexpected token [whatever substring is read]
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"(INVALID_JSON)", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected token.\nI", false);

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme" : "Children")", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected end of string. Expected , or } after key:value pair.\n"
      "\n"
      "^");

  // Usually the response would be
  //    "Expected : between key:value pair.\n"
  //    "{ \"theme\"  \"Children\" }\n"
  //    "           ^");
  // But as with INVALID_JSON Envoy may not read the full string from the upstream connection so may
  // generate its error based on a partial upstream response.
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme"  "Children" })", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Expected : between key:value pair.\n", false);

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme" : "Children" }EXTRA)", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Parsing terminated before end of input.\n", false);
}

std::string createDeepJson(int level, bool valid) {
  std::string begin = R"({"k":)";
  std::string deep_val = R"("v")";
  std::string end = R"(})";
  std::string json;

  for (int i = 0; i < level; ++i) {
    absl::StrAppend(&json, begin);
  }
  if (valid) {
    absl::StrAppend(&json, deep_val);
  }
  for (int i = 0; i < level; ++i) {
    absl::StrAppend(&json, end);
  }
  return json;
}

std::string jsonStrToPbStrucStr(std::string json) {
  Envoy::ProtobufWkt::Struct message;
  std::string structStr;
  TestUtility::loadFromJson(json, message);
  TextFormat::PrintToString(message, &structStr);
  return structStr;
}

TEST_P(GrpcJsonTranscoderIntegrationTest, DeepStruct) {
  HttpIntegrationTest::initialize();
  // Due to the limit of protobuf util, we can only compare to level 32.
  std::string deepJson = createDeepJson(32, true);
  std::string deepProto = "content {" + jsonStrToPbStrucStr(deepJson) + "}";
  testTranscoding<bookstore::EchoStructReqResp, bookstore::EchoStructReqResp>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoStruct"}, {":authority", "host"}},
      deepJson, {deepProto}, {deepProto}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", "application/json"}, {"grpc-status", "0"}},
      R"({"content":)" + deepJson + R"(})");

  // The valid deep struct is parsed successfully.
  // Since we didn't set the response, it return 503.
  // Response body is empty (not a valid JSON), so content type should be application/grpc.
  testTranscoding<bookstore::EchoStructReqResp, bookstore::EchoStructReqResp>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoStruct"}, {":authority", "host"}},
      createDeepJson(100, true), {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "503"}, {"content-type", "application/grpc"}},
      "");

  // The invalid deep struct is detected.
  testTranscoding<bookstore::EchoStructReqResp, bookstore::EchoStructReqResp>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoStruct"}, {":authority", "host"}},
      createDeepJson(100, false), {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected token.\n", false);
}

std::string createLargeJson(int level) {
  std::shared_ptr<ProtobufWkt::Value> cur = std::make_shared<ProtobufWkt::Value>();
  for (int i = 0; i < level - 1; ++i) {
    std::shared_ptr<ProtobufWkt::Value> next = std::make_shared<ProtobufWkt::Value>();
    ProtobufWkt::Value val = ProtobufWkt::Value();
    ProtobufWkt::Value left = ProtobufWkt::Value(*cur);
    ProtobufWkt::Value right = ProtobufWkt::Value(*cur);
    val.mutable_list_value()->add_values()->Swap(&left);
    val.mutable_list_value()->add_values()->Swap(&right);
    (*next->mutable_struct_value()->mutable_fields())["k"] = val;
    cur = next;
  }
  return MessageUtil::getJsonStringFromMessage(*cur, false, false);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, LargeStruct) {
  HttpIntegrationTest::initialize();
  // Create a 40kB json payload.

  std::string largeJson = createLargeJson(12);
  std::string largeProto = "content {" + jsonStrToPbStrucStr(largeJson) + "}";
  testTranscoding<bookstore::EchoStructReqResp, bookstore::EchoStructReqResp>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoStruct"}, {":authority", "host"}},
      largeJson, {largeProto}, {largeProto}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", "application/json"}, {"grpc-status", "0"}},
      R"({"content":)" + largeJson + R"(})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnknownField) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme": "Children", "unknown1": "a", "unknown2" : {"a" : "b"}, "unknown3" : ["a", "b", "c"]})",
      {R"(shelf { theme: "Children" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "30"},
                                      {"grpc-status", "0"}},
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UTF8) {
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "{\"id\":\"20\",\"theme\":\"\xC2\xAE\"}", {"shelf {id : 20 theme: \"®\" }"},
      {"id: 20 theme: \"\xC2\xAE\""}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-type", "application/json"}, {"grpc-status", "0"}},
      R"({"id":"20","theme":"®"})");

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "{\"id\":\"20\",\"theme\":\"\xC3\x28\"}", {}, {""}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}}, R"(Encountered non UTF-8 code points)",
      false);
}

} // namespace
} // namespace Envoy
