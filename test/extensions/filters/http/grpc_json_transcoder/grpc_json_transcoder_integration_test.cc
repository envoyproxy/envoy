#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using Envoy::Protobuf::Message;
using Envoy::Protobuf::TextFormat;
using Envoy::Protobuf::util::MessageDifferencer;
using Envoy::ProtobufUtil::Status;
using Envoy::ProtobufUtil::error::Code;
using Envoy::ProtobufWkt::Empty;

namespace Envoy {

class GrpcJsonTranscoderIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  GrpcJsonTranscoderIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    const std::string filter =
        R"EOF(
            name: envoy.grpc_json_transcoder
            config:
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
            )EOF";
    config_helper_.addFilter(
        fmt::format(filter, TestEnvironment::runfilesPath("/test/proto/bookstore.descriptor")));
    HttpIntegrationTest::initialize();
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
  void testTranscoding(Http::HeaderMap&& request_headers, const std::string& request_body,
                       const std::vector<std::string>& grpc_request_messages,
                       const std::vector<std::string>& grpc_response_messages,
                       const Status& grpc_status, Http::HeaderMap&& response_headers,
                       const std::string& response_body, bool full_response = true) {
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

      Grpc::Decoder grpc_decoder;
      std::vector<Grpc::Frame> frames;
      EXPECT_TRUE(grpc_decoder.decode(upstream_request_->body(), frames));
      EXPECT_EQ(grpc_request_messages.size(), frames.size());

      for (size_t i = 0; i < grpc_request_messages.size(); ++i) {
        RequestType actual_message;
        if (frames[i].length_ > 0) {
          EXPECT_TRUE(actual_message.ParseFromString(frames[i].data_->toString()));
        }
        RequestType expected_message;
        EXPECT_TRUE(TextFormat::ParseFromString(grpc_request_messages[i], &expected_message));

        EXPECT_TRUE(MessageDifferencer::Equivalent(expected_message, actual_message));
      }

      Http::TestHeaderMapImpl response_headers;
      response_headers.insertStatus().value(200);
      response_headers.insertContentType().value(std::string("application/grpc"));
      if (grpc_response_messages.empty()) {
        response_headers.insertGrpcStatus().value(grpc_status.error_code());
        response_headers.insertGrpcMessage().value(grpc_status.error_message());
        upstream_request_->encodeHeaders(response_headers, true);
      } else {
        response_headers.addCopy(Http::LowerCaseString("trailer"), "Grpc-Status");
        response_headers.addCopy(Http::LowerCaseString("trailer"), "Grpc-Message");
        upstream_request_->encodeHeaders(response_headers, false);
        for (const auto& response_message_str : grpc_response_messages) {
          ResponseType response_message;
          EXPECT_TRUE(TextFormat::ParseFromString(response_message_str, &response_message));
          auto buffer = Grpc::Common::serializeBody(response_message);
          upstream_request_->encodeData(*buffer, false);
        }
        Http::TestHeaderMapImpl response_trailers;
        response_trailers.insertGrpcStatus().value(grpc_status.error_code());
        response_trailers.insertGrpcMessage().value(grpc_status.error_message());
        upstream_request_->encodeTrailers(response_trailers);
      }
      EXPECT_TRUE(upstream_request_->complete());
    }

    response->waitForEndStream();
    EXPECT_TRUE(response->complete());

    if (response->headers().get(Http::LowerCaseString("transfer-encoding")) == nullptr ||
        strncmp(
            response->headers().get(Http::LowerCaseString("transfer-encoding"))->value().c_str(),
            "chunked", strlen("chunked")) != 0) {
      EXPECT_EQ(response->headers().get(Http::LowerCaseString("trailer")), nullptr);
    }

    response_headers.iterate(
        [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
          IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
          Http::LowerCaseString lower_key{entry.key().c_str()};
          EXPECT_STREQ(entry.value().c_str(), response->headers().get(lower_key)->value().c_str());
          return Http::HeaderMap::Iterate::Continue;
        },
        response.get());
    if (!response_body.empty()) {
      if (full_response) {
        EXPECT_EQ(response_body, response->body());
      } else {
        EXPECT_TRUE(StringUtil::startsWith(response->body().c_str(), response_body));
      }
    }

    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, GrpcJsonTranscoderIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPost) {
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/shelf"},
                              {":authority", "host"},
                              {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "30"},
                              {"grpc-status", "0"}},
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGet) {
  testTranscoding<Empty, bookstore::ListShelvesResponse>(
      Http::TestHeaderMapImpl{{":method", "GET"}, {":path", "/shelves"}, {":authority", "host"}},
      "", {""}, {R"(shelves { id: 20 theme: "Children" }
          shelves { id: 1 theme: "Foo" } )"},
      Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "69"},
                              {"grpc-status", "0"}},
      R"({"shelves":[{"id":"20","theme":"Children"},{"id":"1","theme":"Foo"}]})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetHttpBody) {
  testTranscoding<Empty, google::api::HttpBody>(
      Http::TestHeaderMapImpl{{":method", "GET"}, {":path", "/index"}, {":authority", "host"}}, "",
      {""}, {R"(content_type: "text/html" data: "<h1>Hello!</h1>" )"}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "text/html"},
                              {"content-length", "15"},
                              {"grpc-status", "0"}},
      R"(<h1>Hello!</h1>)");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetError) {
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100?"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(Code::NOT_FOUND, "Shelf 100 Not Found"),
      Http::TestHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 100 Not Found"}},
      "");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryDelete) {
  testTranscoding<bookstore::DeleteBookRequest, Empty>(
      Http::TestHeaderMapImpl{
          {":method", "DELETE"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      "", {"shelf: 456 book: 123"}, {""}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "2"},
                              {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPatch) {
  testTranscoding<bookstore::UpdateBookRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "PATCH"}, {":path", "/shelves/456/books/123"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 456 book { id: 123 author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 123 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "59"},
                              {"grpc-status", "0"}},
      R"({"id":"123","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryCustom) {
  testTranscoding<bookstore::GetShelfRequest, Empty>(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"}, {":path", "/shelves/456"}, {":authority", "host"}},
      "", {"shelf: 456"}, {""}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"content-type", "application/json"},
                              {"content-length", "2"},
                              {"grpc-status", "0"}},
      "{}");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, BindingAndBody) {
  testTranscoding<bookstore::CreateBookRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "PUT"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      R"({"author" : "Leo Tolstoy", "title" : "War and Peace"})",
      {R"(shelf: 1 book { author: "Leo Tolstoy" title: "War and Peace" })"},
      {R"(id: 3 author: "Leo Tolstoy" title: "War and Peace")"}, Status(),
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"3","author":"Leo Tolstoy","title":"War and Peace"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingGet) {
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"},
      {R"(id: 1 author: "Neal Stephenson" title: "Readme")",
       R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
      Status(), Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"([{"id":"1","author":"Neal Stephenson","title":"Readme"})"
      R"(,{"id":"2","author":"George R.R. Martin","title":"A Game of Thrones"}])");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamingPost) {
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{
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
      Http::TestHeaderMapImpl{{":status", "200"},
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
  // Usually the response would be
  // "Unexpected token.\n"
  //    "INVALID_JSON\n"
  //    "^"
  // If Envoy does a short read of the upstream connection, it may only read part of the
  // string "INVALID_JSON". Envoy will note "Unexpected token [whatever substring is read]
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"(INVALID_JSON)", {}, {}, Status(),
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Unexpected token.\nI", false);

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme" : "Children")", {}, {}, Status(),
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
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
      Http::TestHeaderMapImpl{{":method", "POST"}, {":path", "/shelf"}, {":authority", "host"}},
      R"({ "theme"  "Children" })", {}, {}, Status(),
      Http::TestHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "Expected : between key:value pair.\n", false);
}

} // namespace Envoy
