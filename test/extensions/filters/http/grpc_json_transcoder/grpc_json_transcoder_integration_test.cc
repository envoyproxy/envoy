#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

using absl::Status;
using absl::StatusCode;
using Envoy::Protobuf::TextFormat;
using Envoy::ProtobufWkt::Empty;

namespace Envoy {
namespace {

// A magic header value which marks header as not expected.
constexpr char UnexpectedHeaderValue[] = "Unexpected header value";

std::string ipAndDeferredProcessingParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& p) {
  return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
                     std::get<1>(p.param) ? "WithDeferredProcessing" : "NoDeferredProcessing");
}

// TODO(kbaichoo): Remove parameterizing by deferred processing when the feature
// is enabled by default. The parameterization is to avoid bit rot since it's
// off by default.
class GrpcJsonTranscoderIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  GrpcJsonTranscoderIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {
    // Parameterize with defer processing to prevent bit rot as filter made
    // assumptions of data flow, prior relying on eager processing.
    config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                      deferredProcessing() ? "true" : "false");
  }

  void SetUp() override {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    config_helper_.prependFilter(filter());
  }

protected:
  static constexpr absl::string_view baseFilter() {
    return R"EOF(
name: grpc_json_transcoder
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
  proto_descriptor : "{}"
  services : "bookstore.Bookstore"
)EOF";
  }
  virtual std::string filter() {
    return fmt::format(baseFilter(),
                       TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"));
  }
  template <class RequestType, class ResponseType>
  void testTranscoding(Http::RequestHeaderMap&& request_headers, const std::string& request_body,
                       const std::vector<std::string>& expected_grpc_request_messages,
                       const std::vector<std::string>& grpc_response_messages,
                       const Status& grpc_status, Http::HeaderMap&& expected_response_headers,
                       const std::string& expected_response_body, bool full_response = true,
                       bool always_send_trailers = false,
                       const std::string expected_upstream_request_body = "",
                       bool expect_connection_to_upstream = true,
                       bool expect_response_complete = true) {
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

    if (expect_connection_to_upstream) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }

    if (!expected_grpc_request_messages.empty() || !expected_upstream_request_body.empty()) {
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

      std::string dump;
      for (char ch : upstream_request_->body().toString()) {
        dump += std::to_string(int(ch));
        dump += " ";
      }

      if (!expected_grpc_request_messages.empty()) {
        Grpc::Decoder grpc_decoder;
        std::vector<Grpc::Frame> frames;
        ASSERT_TRUE(grpc_decoder.decode(upstream_request_->body(), frames)) << dump;
        EXPECT_EQ(expected_grpc_request_messages.size(), frames.size());

        for (size_t i = 0; i < expected_grpc_request_messages.size(); ++i) {
          RequestType actual_message;
          if (frames[i].length_ > 0) {
            ASSERT_TRUE(actual_message.ParseFromString(frames[i].data_->toString()));
          }
          RequestType expected_message;
          ASSERT_TRUE(
              TextFormat::ParseFromString(expected_grpc_request_messages[i], &expected_message));
          EXPECT_THAT(actual_message, ProtoEq(expected_message));
        }
      }

      if (!expected_upstream_request_body.empty()) {
        EXPECT_EQ(expected_upstream_request_body, upstream_request_->body().toString());
      }

      Http::TestResponseHeaderMapImpl response_headers;
      response_headers.setStatus(200);
      response_headers.setContentType("application/grpc");
      if (grpc_response_messages.empty() && !always_send_trailers) {
        response_headers.setGrpcStatus(static_cast<uint64_t>(grpc_status.code()));
        response_headers.setGrpcMessage(grpc_status.message());
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
        response_trailers.setGrpcStatus(static_cast<uint64_t>(grpc_status.code()));
        response_trailers.setGrpcMessage(grpc_status.message());
        upstream_request_->encodeTrailers(response_trailers);
      }
      EXPECT_TRUE(upstream_request_->complete());
    }

    if (expect_response_complete) {
      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(response->complete());
    } else {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    }

    if (response->headers().get(Http::LowerCaseString("transfer-encoding")).empty() ||
        !absl::StartsWith(response->headers()
                              .get(Http::LowerCaseString("transfer-encoding"))[0]
                              ->value()
                              .getStringView(),
                          "chunked")) {
      EXPECT_TRUE(response->headers().get(Http::LowerCaseString("trailer")).empty());
    }

    expected_response_headers.iterate(
        [response = response.get()](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
          Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
          if (entry.value() == UnexpectedHeaderValue) {
            EXPECT_TRUE(response->headers().get(lower_key).empty());
          } else {
            if (response->headers().get(lower_key).empty()) {
              ADD_FAILURE() << "Header " << lower_key.get() << " not found.";
            } else {
              EXPECT_EQ(entry.value().getStringView(),
                        response->headers().get(lower_key)[0]->value().getStringView());
            }
          }
          return Http::HeaderMap::Iterate::Continue;
        });
    if (!expected_response_body.empty()) {
      const bool isJsonResponse = response->headers().getContentTypeValue() == "application/json";
      if (full_response && isJsonResponse) {
        const bool isArray = response->body()[0] == '[';

        // test each line of the response for equality
        std::stringstream response_body_stream(response->body());
        std::stringstream expected_response_stream(expected_response_body);
        std::string response_body_chunk;
        std::string expected_response_chunk;
        char delimiter = '\n';

        while (std::getline(response_body_stream, response_body_chunk, delimiter)) {
          std::getline(expected_response_stream, expected_response_chunk, delimiter);

          EXPECT_TRUE(
              TestUtility::jsonStringEqual(response_body_chunk, expected_response_chunk, isArray))
              << "Response mismatch. \nGot : " << response_body_chunk
              << "\nWant: " << expected_response_chunk;
        }

        // verify that both streams have been fully tested
        EXPECT_EQ(response_body_stream.rdbuf()->in_avail(), 0);
        EXPECT_EQ(expected_response_stream.rdbuf()->in_avail(), 0);
      } else if (full_response) {
        EXPECT_EQ(response->body(), expected_response_body);
      } else {
        EXPECT_TRUE(absl::StartsWith(response->body(), expected_response_body))
            << "Response mismatch. \nGot : " << response->body()
            << "\nWant: " << expected_response_body;
      }
    }

    codec_client_->close();
    if (fake_upstream_connection_) {
      ASSERT_TRUE(fake_upstream_connection_->close());
      ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    }
  }

  // override configuration on per-route basis
  void overrideConfig(const std::string& json_config) {

    envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder per_route_config;
    TestUtility::loadFromJson(json_config, per_route_config);
    ConfigHelper::HttpModifierFunction modifier =
        [per_route_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                cfg) {
          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["grpc_json_transcoder"].PackFrom(per_route_config);
        };

    config_helper_.addConfigModifier(modifier);
  }

  bool deferredProcessing() const { return std::get<1>(GetParam()); }
};

class GrpcJsonTranscoderIntegrationTestWithSizeLimit : public GrpcJsonTranscoderIntegrationTest {
protected:
  std::string filter() override {
    return fmt::format(baseFilter(),
                       TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")) +
           fmt::format(+R"(
  max_request_body_size: {}
  max_response_body_size: {}
)",
                       maxBodySize(), maxBodySize());
  }
  virtual uint32_t maxBodySize() const PURE;
};

class GrpcJsonTranscoderIntegrationTestWithSizeLimit1024
    : public GrpcJsonTranscoderIntegrationTestWithSizeLimit {
protected:
  uint32_t maxBodySize() const override { return 1024; }
};

class GrpcJsonTranscoderIntegrationTestWithSizeLimit1
    : public GrpcJsonTranscoderIntegrationTestWithSizeLimit {
protected:
  uint32_t maxBodySize() const override { return 1; }
};

class GrpcJsonTranscoderIntegrationTestWithSizeLimit35
    : public GrpcJsonTranscoderIntegrationTestWithSizeLimit {
protected:
  uint32_t maxBodySize() const override { return 35; }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsDeferredProcessing, GrpcJsonTranscoderIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipAndDeferredProcessingParamsToString);
INSTANTIATE_TEST_SUITE_P(
    IpVersionsDeferredProcessing, GrpcJsonTranscoderIntegrationTestWithSizeLimit1024,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipAndDeferredProcessingParamsToString);
INSTANTIATE_TEST_SUITE_P(
    IpVersionsDeferredProcessing, GrpcJsonTranscoderIntegrationTestWithSizeLimit1,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipAndDeferredProcessingParamsToString);
INSTANTIATE_TEST_SUITE_P(
    IpVersionsDeferredProcessing, GrpcJsonTranscoderIntegrationTestWithSizeLimit35,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipAndDeferredProcessingParamsToString);

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

TEST_P(GrpcJsonTranscoderIntegrationTest, TestParamUnescapePlus) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              query_param_unescape_plus: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  // Test '+',  'query_param_unescape_plus' is true, '-' is converted to space.
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.theme=Children+Books"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { theme: "Children Books" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, QueryParamsDecodedName) {
  HttpIntegrationTest::initialize();

  // json_name = "search[decoded]", "search%5Bdecoded%5D" should work
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.search%5Bdecoded%5D=Google"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { search_decoded: "Google" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"20","theme":"Children"})");

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.search[decoded]=Google"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { search_decoded: "Google" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"20","theme":"Children"})");

  // json_name = "search%5Bencoded%5D", "search[encode]" should fail.
  // It is tested in test case "DecodedQueryParameterWithEncodedJsonName"
  // in json_transcoder_filter_test.cc.

  // json_name = "search%5Bencoded%5D", "search%5Bencoded%5D" should work.
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.search%5Bencoded%5D=Google"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { search_encoded: "Google" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"},
          {"content-type", "application/json"},
      },
      R"({"id":"20","theme":"Children"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, QueryParams) {
  HttpIntegrationTest::initialize();
  // 1. Binding theme='Children' in CreateShelfRequest
  // Using the following HTTP template:
  //   POST /shelves
  //   body: shelf

  // Test '+',  'query_param_unescape_plus' is false by default, '-' is not converted to space.
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf?shelf.theme=Children+Books"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {R"(shelf { theme: "Children+Books" })"}, {R"(id: 20 theme: "Children" )"}, Status(),
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

// Test JSON to proto translation on proto enum value.
// By default, a JSON enum value string has to match the case specified in the proto.
// Normally proto enum values are specified in all upper case.
TEST_P(GrpcJsonTranscoderIntegrationTest, TestEnumValueCaseMatch) {
  HttpIntegrationTest::initialize();

  // JSON enum value string "FEMALE" should work.
  testTranscoding<bookstore::Author, bookstore::Author>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoAuthor"}, {":authority", "host"}},
      R"({"id":"1234","gender":"FEMALE"})", {R"(id: 1234 gender: FEMALE)"},
      {R"(id: 1234 gender: FEMALE)"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "31"},
                                      {"grpc-status", "0"}},
      R"({"id":"1234","gender":"FEMALE"})");

  // JSON enum value string "Female" should fail.
  testTranscoding<bookstore::Author, bookstore::Author>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoAuthor"}, {":authority", "host"}},
      R"({"id":"1234","gender":"Female"})", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "gender: invalid value \"Female\" for type type.googleapis.com/bookstore.Author.Gender",
      false);
}

// Test JSON to proto translation on proto enum value.
// By default, a JSON enum value string has to match the case specified in the proto.
// Normally proto enum values are specified in all upper case.
// After enable the "case_insensitive_enum_parsing" flag,
// JSON enum value string can be in any case.
TEST_P(GrpcJsonTranscoderIntegrationTest, TestEnumValueIgnoreCase) {

  // Enable case_insensitive_enum_parsing flag
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              case_insensitive_enum_parsing : true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();

  // JSON enum value string "FEMALE" should work.
  testTranscoding<bookstore::Author, bookstore::Author>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoAuthor"}, {":authority", "host"}},
      R"({"id":"1234","gender":"FEMALE"})", {R"(id: 1234 gender: FEMALE)"},
      {R"(id: 1234 gender: FEMALE)"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "31"},
                                      {"grpc-status", "0"}},
      R"({"id":"1234","gender":"FEMALE"})");

  // JSON enum value string "Female" should work too.
  testTranscoding<bookstore::Author, bookstore::Author>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoAuthor"}, {":authority", "host"}},
      R"({"id":"1234","gender":"Female"})", {R"(id: 1234 gender: FEMALE)"},
      {R"(id: 1234 gender: FEMALE)"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "31"},
                                      {"grpc-status", "0"}},
      R"({"id":"1234","gender":"FEMALE"})");
}

// Test newline-delimited stream translation.
// By default, streams are aggregated into a JSON Array.
// Newline-delimited streams return each message with a newline termination instead.
TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingNewlineDelimitedGet) {
  // Enable stream_newline_delimited flag
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              print_options :
                stream_newline_delimited : true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();

  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"},
      {R"(id: 1 author: "Neal Stephenson" title: "Reamde")",
       R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
      Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"({"id":"1","author":"Neal Stephenson","title":"Reamde"})"
      "\n"
      R"({"id":"2","author":"George R.R. Martin","title":"A Game of Thrones"})");
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

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamGetHttpBody) {
  HttpIntegrationTest::initialize();

  // 1. Normal streaming get
  testTranscoding<Empty, google::api::HttpBody>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/indexStream"}, {":authority", "host"}},
      "", {""},
      {R"(content_type: "text/html" data: "<h1>Hello!</h1>")",
       R"(content_type: "text/plain" data: "Hello!")"},
      Status(), Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}},
      R"(<h1>Hello!</h1>)"
      R"(Hello!)");

  // 2. Empty response (trailers only) from streaming backend, with a gRPC error.
  testTranscoding<Empty, google::api::HttpBody>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/indexStream"}, {":authority", "host"}},
      "", {""}, {}, Status(StatusCode::kNotFound, "Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"}, {"content-type", "application/json"}},
      "");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamGetHttpBodyMultipleFramesInData) {
  HttpIntegrationTest::initialize();

  // testTranscoding() does not provide grpc multiframe support.
  // Since this is one-off it does not make sense to even more
  // complicate this function.
  //
  // Make request to gRPC upstream
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/indexStream"},
      {":authority", "host"},
  });
  waitForNextUpstreamRequest();

  // Send multi-framed gRPC response
  // Headers
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/grpc");
  upstream_request_->encodeHeaders(response_headers, false);
  // Payload
  google::api::HttpBody grpcMsg;
  EXPECT_TRUE(TextFormat::ParseFromString(R"(content_type: "text/plain" data: "Hello")", &grpcMsg));
  Buffer::OwnedImpl response_buffer;
  for (size_t i = 0; i < 3; i++) {
    auto frame = Grpc::Common::serializeToGrpcFrame(grpcMsg);
    response_buffer.add(*frame);
  }
  upstream_request_->encodeData(response_buffer, false);
  // Trailers
  Http::TestResponseTrailerMapImpl response_trailers;
  auto grpc_status = Status();
  response_trailers.setGrpcStatus(static_cast<uint64_t>(grpc_status.code()));
  response_trailers.setGrpcMessage(grpc_status.message());
  upstream_request_->encodeTrailers(response_trailers);
  EXPECT_TRUE(upstream_request_->complete());

  // Wait for complete / check body to have 3 frames joined
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ(response->body(), "HelloHelloHello");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, StreamGetHttpBodyFragmented) {
  HttpIntegrationTest::initialize();

  // Make request to gRPC upstream
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/indexStream"},
      {":authority", "host"},
  });
  waitForNextUpstreamRequest();

  // Send fragmented gRPC response
  // Headers
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/grpc");
  upstream_request_->encodeHeaders(response_headers, false);
  // Fragmented payload
  google::api::HttpBody http_body;
  http_body.set_content_type("text/plain");
  http_body.set_data(std::string(1024, 'a'));
  // Fragment gRPC frame into 2 buffers equally divided
  Buffer::OwnedImpl fragment1;
  auto fragment2 = Grpc::Common::serializeToGrpcFrame(http_body);
  fragment1.move(*fragment2, fragment2->length() / 2);
  upstream_request_->encodeData(fragment1, false);
  upstream_request_->encodeData(*fragment2, false);
  // Trailers
  Http::TestResponseTrailerMapImpl response_trailers;
  auto grpc_status = Status();
  response_trailers.setGrpcStatus(static_cast<uint64_t>(grpc_status.code()));
  response_trailers.setGrpcMessage(grpc_status.message());
  upstream_request_->encodeTrailers(response_trailers);
  EXPECT_TRUE(upstream_request_->complete());

  // Wait for complete
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Ensure that body was actually replaced
  EXPECT_EQ(response->body(), http_body.data());
  // As well as content-type header
  auto content_type = response->headers().get(Http::LowerCaseString("content-type"));
  EXPECT_EQ("text/plain", content_type[0]->value().getStringView());
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
      "", {"shelf: 100"}, {}, Status(StatusCode::kNotFound, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 100 Not Found"}},
      "");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryGetError1) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              ignore_unknown_query_parameters : true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1&shelf=9999"},
                                     {":authority", "host"}},
      "", {"shelf: 9999"}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "");
}

// Test an upstream that returns an error in a trailer-only response.
TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryErrorConvertedToJson) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(StatusCode::kNotFound, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"},
                                      {"content-type", "application/json"},
                                      {"grpc-status", UnexpectedHeaderValue},
                                      {"grpc-message", UnexpectedHeaderValue}},
      R"({"code":5,"message":"Shelf 100 Not Found"})");
}

// Upstream sends headers (e.g. sends metadata), and then sends trailer with an error.
TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryErrorInTrailerConvertedToJson) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/100"}, {":authority", "host"}},
      "", {"shelf: 100"}, {}, Status(StatusCode::kNotFound, "Shelf 100 Not Found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"},
                                      {"content-type", "application/json"},
                                      {"grpc-status", UnexpectedHeaderValue},
                                      {"grpc-message", UnexpectedHeaderValue}},
      R"({"code":5,"message":"Shelf 100 Not Found"})", true, true);
}

// Streaming backend returns an error in a trailer-only response.
TEST_P(GrpcJsonTranscoderIntegrationTest, StreamingErrorConvertedToJson) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor: "{}"
              services: "bookstore.Bookstore"
              convert_grpc_status: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::ListBooksRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/37/books"}, {":authority", "host"}},
      "", {"shelf: 37"}, {}, Status(StatusCode::kNotFound, "Shelf 37 Not Found"),
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

TEST_P(GrpcJsonTranscoderIntegrationTest, WrongBindingType) {
  HttpIntegrationTest::initialize();
  // Http template is "/shelves/{shelf}/books/{book.id}" and field "book.id" is int64.
  // But path is "/shelves/456/books/abc" and the {book.id} segment is a string.
  // The request should be rejected with 400.

  // The bug reported in https://github.com/envoyproxy/envoy/issues/22926 is:
  // if the request body is not empty, the request is rejected as expected.
  // Buf if the request body is empty, the request is not rejected, the book.id field is ignored.

  // This test to verify the bug has been fixed. The request in both cases should be rejected.

  // Request with a non-empty request body.
  testTranscoding<bookstore::UpdateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PATCH"}, {":path", "/shelves/456/books/abc"}, {":authority", "host"}},
      "{}", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "book.id: invalid value \"abc\" for type TYPE_INT64", false);

  // The request with an empty request body.
  // The request is rejected in decodeHeaders so upstream connection is not created.
  // Here we need to pass "expect_connection_to_upstream=false".
  testTranscoding<bookstore::UpdateBookRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "PATCH"}, {":path", "/shelves/456/books/abc"}, {":authority", "host"}},
      "", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "400"}, {"content-type", "text/plain"}},
      "book.id: invalid value \"abc\" for type TYPE_INT64", false, false, "", false);
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
      "", {"shelf: 37"}, {}, Status(StatusCode::kNotFound, "Shelf 37 not found"),
      Http::TestResponseHeaderMapImpl{{":status", "404"}, {"content-type", "application/json"}},
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
  // Lower the timeout for a incomplete response.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        virtual_host->mutable_routes(0)->mutable_route()->mutable_idle_timeout()->set_seconds(5);
      });

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
  // Since we didn't set a response, it will time out.
  // Response body is empty (not a valid JSON), so the error response is plaintext.
  testTranscoding<bookstore::EchoStructReqResp, bookstore::EchoStructReqResp>(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/echoStruct"}, {":authority", "host"}},
      createDeepJson(100, true), {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "504"}, {"content-type", "text/plain"}}, "");

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
  return MessageUtil::getJsonStringFromMessageOrError(*cur, false, false);
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

TEST_P(GrpcJsonTranscoderIntegrationTest, UnknownFieldInRequest) {
  // Request JSON has many fields that are unknown to the request proto message.
  // They are discarded.
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

// Test proto to json transcoding with an unknown field in the response message.
// gRPC server may use a updated proto with a new field, but Envoy transcoding
// filter could use an old proto descriptor without that field. That fields is unknown
// to the Envoy transcoder filter. Expected result: the unknown field is discarded,
// other fields should be transcoded properly.
TEST_P(GrpcJsonTranscoderIntegrationTest, UnknownResponse) {
  // The mocked upstream proto response message is bookstore::BigBook which has
  // all 3 fields. But the proto descriptor used by the Envoy transcoder filter is using
  // bookstore::OldBigBook which is missing the `field1` field.
  HttpIntegrationTest::initialize();
  // The bug is ZeroCopyInputStreamImpl::Skip() which is not implemented.
  // In order to trigger a call to that function, the response message has to be big enough
  // so it is stored in multiple slices.
  const std::string field1_value = std::string(32 * 1024, 'O');
  const std::string response_body =
      fmt::format(R"(field1: "{}" field2: "field2_value" field3: "field3_value" )", field1_value);
  testTranscoding<Empty, bookstore::BigBook>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/bigbook"}, {":authority", "host"}},
      "", {""}, {response_body}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "49"},
                                      {"grpc-status", "0"}},
      R"({"field2":"field2_value","field3":"field3_value"})");
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

TEST_P(GrpcJsonTranscoderIntegrationTest, DisableRequestValidation) {
  HttpIntegrationTest::initialize();

  // Transcoding does not occur from a request with the gRPC content type.
  // We verify the request is not transcoded because the upstream receives the same JSON body.
  // We verify the response is not transcoded because the HTTP status code does not match the gRPC
  // status.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");

  // Transcoding does not occur when unknown path is called.
  // HTTP Request to is passed directly to gRPC backend.
  // gRPC response is passed directly to HTTP client.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/unknown/path"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");

  // Transcoding does not occur when unknown query param is included.
  // HTTP Request to is passed directly to gRPC backend.
  // gRPC response is passed directly to HTTP client.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, RejectUnknownMethod) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              request_validation_options:
                reject_unknown_method: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();

  // Transcoding does not occur from a request with the gRPC content type, even with an unknown
  // path. We verify the request is not transcoded because the upstream receives the same JSON body.
  // We verify the response is not transcoded because the HTTP status code does not match the gRPC
  // status.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/unknown/path"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");

  // Transcoding does not occur when unknown path is called.
  // The request is rejected.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/unknown/path"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {}, {}, Status(), Http::TestResponseHeaderMapImpl{{":status", "404"}},
      "Could not resolve /unknown/path to a method.", true, false, "", false);

  // Transcoding does not occur when unknown query param is included.
  // HTTP Request to is passed directly to gRPC backend.
  // gRPC response is passed directly to HTTP client.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, RejectUnknownQueryParam) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              request_validation_options:
                reject_unknown_query_parameters: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();

  // Transcoding does not occur from a request with the gRPC content type, even with unknown query
  // params. We verify the request is not transcoded because the upstream receives the same JSON
  // body. We verify the response is not transcoded because the HTTP status code does not match the
  // gRPC status.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");

  // Transcoding does not occur when unknown path is called.
  // HTTP Request to is passed directly to gRPC backend.
  // gRPC response is passed directly to HTTP client.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/unknown/path"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({ "theme" : "Children")", {}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "", true, false, R"({ "theme" : "Children")");

  // Transcoding does not occur when unknown query param is included.
  // The request is rejected.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/shelves/100?unknown=1"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {}, {}, Status(), Http::TestResponseHeaderMapImpl{{":status", "400"}},
      "Could not find field \"unknown\" in the type \"bookstore.GetShelfRequest\".", true, false,
      "", false);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, EnableRequestValidationIgnoreQueryParam) {
  constexpr absl::string_view filter =
      R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              proto_descriptor : "{}"
              services : "bookstore.Bookstore"
              ignore_unknown_query_parameters : true
              request_validation_options:
                reject_unknown_method: true
                reject_unknown_query_parameters: true
            )EOF";
  config_helper_.prependFilter(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  HttpIntegrationTest::initialize();

  // When strict mode is enabled with ignore unknown query params,
  // the request is not rejected and transcoding occurs.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/9999?unknown=1"}, {":authority", "host"}},
      "", {"shelf: 9999"}, {}, Status(StatusCode::kNotFound, "Shelf 9999 Not Found"),
      Http::TestResponseHeaderMapImpl{
          {":status", "404"}, {"grpc-status", "5"}, {"grpc-message", "Shelf 9999 Not Found"}},
      "");

  // Transcoding does not occur when unknown path is called.
  // The request is rejected, even though it has unknown query params.
  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/unknown/path?unknown=1"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      "", {}, {}, Status(), Http::TestResponseHeaderMapImpl{{":status", "404"}},
      "Could not resolve /unknown/path to a method.", true, false, "", false);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPostRequestExceedsBufferLimit) {
  // Request body is more than 8 bytes.
  config_helper_.setBufferLimits(2 << 20, 8);
  HttpIntegrationTest::initialize();

  testTranscoding<bookstore::GetShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme" : "Children"})", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "413"}},
      "Request rejected because the transcoder's internal buffer size exceeds the configured "
      "limit.",
      true, false, "", true);
}

TEST_P(GrpcJsonTranscoderIntegrationTestWithSizeLimit1024,
       UnaryPostRequestExceedsBufferLimitButNotSizeLimitShouldWork) {
  // Request body is more than 8 bytes, but size limit from config allows 1024 bytes.
  config_helper_.setBufferLimits(8, 8);
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

TEST_P(GrpcJsonTranscoderIntegrationTestWithSizeLimit1,
       UnaryPostRequestExceedsSizeLimitButNotBufferLimitShouldFail) {
  // Request body is less than buffer 35 bytes, but size limit from config is too small.
  config_helper_.setBufferLimits(2 << 20, 35);
  HttpIntegrationTest::initialize();

  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme" : "Children"})", {}, {}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "413"}},
      "Request rejected because the transcoder's internal buffer size exceeds the configured "
      "limit.",
      true, false, "", true);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPostResponseExceedsBufferLimit) {
  // Request body is less than 35 bytes.
  // Response body is more than 35 bytes.
  config_helper_.setBufferLimits(2 << 20, 35);
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children 0123456789 0123456789 0123456789 0123456789" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "500"}, {"content-type", "text/plain"}, {"content-length", "99"}},
      "Response not transcoded because the transcoder's internal buffer size exceeds the "
      "configured limit.");
}

TEST_P(GrpcJsonTranscoderIntegrationTestWithSizeLimit1024,
       UnaryPostResponseExceedsBufferLimitButNotSizeLimitShouldWork) {
  // Request body is less than 35 bytes.
  // Response body is more than 35 bytes, but size limit from config allows 1024 bytes.
  config_helper_.setBufferLimits(2 << 20, 35);
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children 0123456789 0123456789 0123456789 0123456789" )"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/json"},
                                      {"content-length", "74"},
                                      {"grpc-status", "0"}},
      R"({"id":"20","theme":"Children 0123456789 0123456789 0123456789 0123456789"})");
}

TEST_P(GrpcJsonTranscoderIntegrationTestWithSizeLimit35,
       UnaryPostResponseExceedsSizeLimitButNotBufferLimitShouldFail) {
  // Request body is less than 500 bytes and less than size limit 35.
  // Response body is less than 500 bytes, but more than size limit 35.
  config_helper_.setBufferLimits(2 << 20, 500);
  HttpIntegrationTest::initialize();
  testTranscoding<bookstore::CreateShelfRequest, bookstore::Shelf>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/shelf"},
                                     {":authority", "host"},
                                     {"content-type", "application/json"}},
      R"({"theme": "Children"})", {R"(shelf { theme: "Children" })"},
      {R"(id: 20 theme: "Children 0123456789 0123456789 0123456789 0123456789" )"}, Status(),
      Http::TestResponseHeaderMapImpl{
          {":status", "500"}, {"content-type", "text/plain"}, {"content-length", "99"}},
      "Response not transcoded because the transcoder's internal buffer size exceeds the "
      "configured limit.");
}

TEST_P(GrpcJsonTranscoderIntegrationTest, UnaryPostHttpBodyRequestExceedsBufferLimit) {
  // Request body is more than 8 bytes.
  config_helper_.setBufferLimits(2 << 20, 8);
  HttpIntegrationTest::initialize();

  testTranscoding<google::api::HttpBody, google::api::HttpBody>(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/echoRawBody"},
                                     {":authority", "host"},
                                     {"content-type", "text/plain"}},
      R"(hello world!)", {}, {}, Status(), Http::TestResponseHeaderMapImpl{{":status", "413"}},
      "Request rejected because the transcoder's internal buffer size exceeds the configured "
      "limit.",
      true, false, "", true);
}

TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingGetExceedsBufferLimit) {
  config_helper_.setBufferLimits(2 << 20, 60);
  HttpIntegrationTest::initialize();

  // Under limit: A single response message is less than 60 bytes.
  // Messages transcoded successfully.
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"}, {R"(id: 1 author: "Neal Stephenson" title: "Readme")"}, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      R"([{"id":"1","author":"Neal Stephenson","title":"Readme"}])");

  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // Over limit: The server streams two response messages. Because this is
    // larger than the buffer limits, we end up buffering both results in the
    // codec towards the upstream. When we finally process the buffered data, we
    // end up resetting the stream as we've over the transcoder limit.
    testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
        Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
        "", {"shelf: 1"},
        {R"(id: 1 author: "Neal Stephenson" title: "Readme")",
         R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
        Status(),
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
        /*expected_response_body=*/"", false, false, "", true,
        /*expect_response_complete=*/false);

  } else {
    // Over limit: The server streams two response messages. Even through the transcoder
    // handles them independently, portions of the first message are still in the
    // internal buffers while the second one is processed.
    //
    // Because the headers and body is already sent, the stream is closed with
    // an incomplete response.
    testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
        Http::TestRequestHeaderMapImpl{
            {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
        "", {"shelf: 1"},
        {R"(id: 1 author: "Neal Stephenson" title: "Readme")",
         R"(id: 2 author: "George R.R. Martin" title: "A Game of Thrones")"},
        Status(),
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
        // Incomplete response, not valid JSON.
        R"([{"id":"1","author":"Neal Stephenson","title":"Readme"})", false, false, "", true,
        /*expect_response_complete=*/false);
  }
}

TEST_P(GrpcJsonTranscoderIntegrationTest, ServerStreamingGetUnderBufferLimit) {
  const int num_messages = 100;
  const std::string grpc_response_message = R"(id: 1 author: "Neal Stephenson" title: "Readme")";
  // The upstream will encode all of the response back to back, as such some of
  // the responses will cluster together. It's unlikely that a majority of them
  // will have been sent to the Envoy before it has streamed them to the
  // downstream.
  config_helper_.setBufferLimits(2 << 20, 60 * grpc_response_message.size());
  HttpIntegrationTest::initialize();

  // Craft multiple response messages. IF combined together, they exceed the buffer limit.
  std::vector<std::string> grpc_response_messages;
  grpc_response_messages.reserve(num_messages);
  for (int i = 0; i < num_messages; i++) {
    grpc_response_messages.push_back(grpc_response_message);
  }

  // Craft expected response.
  std::vector<std::string> expected_json_messages;
  expected_json_messages.reserve(num_messages);
  for (int i = 0; i < num_messages; i++) {
    expected_json_messages.push_back(R"({"id":"1","author":"Neal Stephenson","title":"Readme"})");
  }
  std::string expected_json_response =
      absl::StrCat("[", absl::StrJoin(expected_json_messages, ","), "]");

  // Under limit: Even though multiple messages are sent from the upstream, they are transcoded
  // while streaming. The buffer limit is never hit. At most two messages are ever in the internal
  // buffers. Transcoding succeeds.
  testTranscoding<bookstore::ListBooksRequest, bookstore::Book>(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/shelves/1/books"}, {":authority", "host"}},
      "", {"shelf: 1"}, grpc_response_messages, Status(),
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      expected_json_response);
}

// TODO(nareddyt): Refactor testTranscoding and add a test case for client streaming under/over
// buffer limit. Will do in a separate PR to minimize diff.

TEST_P(GrpcJsonTranscoderIntegrationTest, RouteDisabled) {
  overrideConfig(R"EOF({"services": [], "proto_descriptor_bin": ""})EOF");
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/shelves"}, {":authority", "host"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
};

class OverrideConfigGrpcJsonTranscoderIntegrationTest : public GrpcJsonTranscoderIntegrationTest {
public:
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    // creates filter but doesn't apply it to bookstore services
    const std::string filter =
        R"EOF(
            name: grpc_json_transcoder
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder
              "proto_descriptor": ""
            )EOF";
    config_helper_.prependFilter(filter);
  }
};
INSTANTIATE_TEST_SUITE_P(
    IpVersionsDeferredProcessing, OverrideConfigGrpcJsonTranscoderIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipAndDeferredProcessingParamsToString);

TEST_P(OverrideConfigGrpcJsonTranscoderIntegrationTest, RouteOverride) {
  // add bookstore per-route override
  constexpr absl::string_view filter =
      R"EOF({{
              "services": ["bookstore.Bookstore"],
              "proto_descriptor": "{}"
          }})EOF";
  overrideConfig(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));

  HttpIntegrationTest::initialize();

  // testing the path that's defined in bookstore.descriptor file (should work  the same way
  // as it does when grpc filter is applied to base config)
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
};

} // namespace
} // namespace Envoy
