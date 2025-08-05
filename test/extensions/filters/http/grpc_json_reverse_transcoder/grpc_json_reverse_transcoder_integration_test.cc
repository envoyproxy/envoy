#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"

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

using Envoy::Protobuf::util::MessageDifferencer;

namespace Envoy {
namespace {

class GrpcJsonReverseTranscoderIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  GrpcJsonReverseTranscoderIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    config_helper_.prependFilter(filter());
  }

protected:
  static constexpr absl::string_view baseFilter() {
    return R"EOF(
name: grpc_json_reverse_transcoder
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_json_reverse_transcoder.v3.GrpcJsonReverseTranscoder
  descriptor_path: "{}"
)EOF";
  }

  virtual std::string filter() {
    return fmt::format(baseFilter(),
                       TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"));
  }

  void overrideConfig(const std::string& json_config) {
    envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
        per_route_config;
    TestUtility::loadFromJson(json_config, per_route_config);
    ConfigHelper::HttpModifierFunction modifier =
        [per_route_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                cfg) {
          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["grpc_json_reverse_transcoder"].PackFrom(per_route_config);
        };
    config_helper_.addConfigModifier(modifier);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GrpcJsonReverseTranscoderIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GrpcJsonReverseTranscoderIntegrationTest, SimpleRequest) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/bookstore.Bookstore/UpdateBook"},
                                                  {"content-type", "application/grpc"}});
  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  bookstore::UpdateBookRequest update_book_request;
  update_book_request.set_shelf(12345);
  update_book_request.mutable_book()->set_id(123);
  update_book_request.mutable_book()->set_title("Kids book");
  update_book_request.mutable_book()->set_author("John Doe");
  auto request_data = Grpc::Common::serializeToGrpcFrame(update_book_request);
  codec_client_->sendData(*request_encoder_, *request_data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  std::string expected_request = "{\"author\":\"John Doe\",\"id\":\"123\",\"title\":\"Kids book\"}";
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().ContentType,
                             Http::Headers::get().ContentTypeValues.Json));
  EXPECT_THAT(
      upstream_request_->headers(),
      ContainsHeader(Http::Headers::get().ContentLength, std::to_string(expected_request.size())));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Path, "/shelves/12345/books/123"));
  EXPECT_EQ(upstream_request_->body().toString(), expected_request);

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data;
  response_data.add("{\"id\":123,\"author\":\"John Doe\",\"title\":\"Kids book\"}");
  upstream_request_->encodeData(response_data, true);

  EXPECT_TRUE(upstream_request_->complete());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().ContentType,
                                                  Http::Headers::get().ContentTypeValues.Grpc));

  bookstore::Book expected_book;
  expected_book.set_id(123);
  expected_book.set_title("Kids book");
  expected_book.set_author("John Doe");

  Buffer::OwnedImpl transcoded_buffer{response->body()};

  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder.decode(transcoded_buffer, frames);

  bookstore::Book book;
  book.ParseFromString(frames[0].data_->toString());

  EXPECT_TRUE(MessageDifferencer::Equals(expected_book, book));

  EXPECT_THAT(*response->trailers(), ContainsHeader(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(GrpcJsonReverseTranscoderIntegrationTest, HttpBodyRequestResponse) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/bookstore.Bookstore/EchoRawBody"},
                                                  {"content-type", "application/grpc"}});

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  std::string request_str = "This is just a plain text";
  google::api::HttpBody http_body;
  http_body.set_content_type(Http::Headers::get().ContentTypeValues.Text);
  http_body.set_data(request_str);
  auto request_data = Grpc::Common::serializeToGrpcFrame(http_body);

  codec_client_->sendData(*request_encoder_, *request_data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().ContentType,
                             Http::Headers::get().ContentTypeValues.Text));
  EXPECT_THAT(upstream_request_->headers(), ContainsHeader(Http::Headers::get().ContentLength,
                                                           std::to_string(request_str.size())));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Path, "/echoRawBody"));
  EXPECT_EQ(upstream_request_->body().toString(), request_str);

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Html);

  upstream_request_->encodeHeaders(response_headers, false);

  std::string response_str = "<h1>Some HTML content</h1>";
  Buffer::OwnedImpl response_data;
  response_data.add(response_str);
  upstream_request_->encodeData(response_data, true);

  EXPECT_TRUE(upstream_request_->complete());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().ContentType,
                                                  Http::Headers::get().ContentTypeValues.Grpc));

  google::api::HttpBody expected_res;
  expected_res.set_content_type(Http::Headers::get().ContentTypeValues.Html);
  expected_res.set_data(response_str);

  Buffer::OwnedImpl transcoded_buffer{response->body()};
  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder.decode(transcoded_buffer, frames);

  google::api::HttpBody transcoded_res;
  transcoded_res.ParseFromString(frames[0].data_->toString());

  EXPECT_TRUE(MessageDifferencer::Equals(expected_res, transcoded_res));

  EXPECT_THAT(*response->trailers(), ContainsHeader(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(GrpcJsonReverseTranscoderIntegrationTest, NestedHttpBodyRequest) {
  constexpr absl::string_view filter = R"EOF({{
"descriptor_path": "{}",
"api_version_header": "Api-Version"
}})EOF";
  overrideConfig(
      fmt::format(filter, TestEnvironment::runfilesPath("test/proto/bookstore.descriptor")));
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers(
      {{":scheme", "http"},
       {":method", "POST"},
       {":authority", "foo"},
       {":path", "/bookstore.Bookstore/CreateBookHttpBody"},
       {"content-type", "application/grpc"},
       {"api-version", "v2"}});

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  std::string book_str = "{\"author\":\"John Doe\",\"id\":123,\"title\":\"Kids book\"}";
  bookstore::CreateBookHttpBodyRequest request;
  request.set_shelf(12345);
  request.mutable_book()->set_content_type(Http::Headers::get().ContentTypeValues.Json);
  request.mutable_book()->set_data(book_str);
  auto request_data = Grpc::Common::serializeToGrpcFrame(request);

  codec_client_->sendData(*request_encoder_, *request_data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().ContentType,
                             Http::Headers::get().ContentTypeValues.Json));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().ContentLength, std::to_string(book_str.size())));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Path, "/v2/shelves/12345/books"));
  EXPECT_EQ(upstream_request_->body().toString(), book_str);

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Html);

  upstream_request_->encodeHeaders(response_headers, false);

  std::string response_str = "<h1>Book created successfully</h1>";
  Buffer::OwnedImpl response_data;
  response_data.add(response_str);
  upstream_request_->encodeData(response_data, true);

  EXPECT_TRUE(upstream_request_->complete());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().ContentType,
                                                  Http::Headers::get().ContentTypeValues.Grpc));

  google::api::HttpBody expected_res;
  expected_res.set_content_type(Http::Headers::get().ContentTypeValues.Html);
  expected_res.set_data(response_str);

  Buffer::OwnedImpl transcoded_buffer{response->body()};
  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder.decode(transcoded_buffer, frames);

  google::api::HttpBody transcoded_res;
  transcoded_res.ParseFromString(frames[0].data_->toString());

  EXPECT_TRUE(MessageDifferencer::Equals(expected_res, transcoded_res));

  EXPECT_THAT(*response->trailers(), ContainsHeader(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(GrpcJsonReverseTranscoderIntegrationTest, RequestWithQueryParams) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers(
      {{":scheme", "http"},
       {":method", "POST"},
       {":authority", "foo"},
       {":path", "/bookstore.Bookstore/ListBooksNonStreaming"},
       {"content-type", "application/grpc"}});

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  bookstore::ListBooksRequest list_books;
  list_books.set_shelf(12345);
  list_books.set_author(567);
  list_books.set_theme("Science Fiction");
  auto request_data = Grpc::Common::serializeToGrpcFrame(list_books);
  codec_client_->sendData(*request_encoder_, *request_data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Method, Http::Headers::get().MethodValues.Get));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Path,
                             "/shelves/12345/books:unary?author=567&theme=Science%20Fiction"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);

  upstream_request_->encodeHeaders(response_headers, false);

  std::string response_str =
      "{\"books\":[{\"id\":123,\"author\":\"John Doe\",\"title\":\"Kids book\"}]}";
  Buffer::OwnedImpl response_data;
  response_data.add(response_str);
  upstream_request_->encodeData(response_data, true);

  EXPECT_TRUE(upstream_request_->complete());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().ContentType,
                                                  Http::Headers::get().ContentTypeValues.Grpc));

  bookstore::ListBooksResponse expected_res;
  auto* book = expected_res.add_books();
  book->set_id(123);
  book->set_title("Kids book");
  book->set_author("John Doe");

  Buffer::OwnedImpl transcoded_buffer{response->body()};
  Grpc::Decoder decoder;
  std::vector<Grpc::Frame> frames;
  std::ignore = decoder.decode(transcoded_buffer, frames);

  bookstore::ListBooksResponse transcoded_res;
  transcoded_res.ParseFromString(frames[0].data_->toString());

  EXPECT_TRUE(MessageDifferencer::Equals(expected_res, transcoded_res));

  EXPECT_THAT(*response->trailers(), ContainsHeader(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(GrpcJsonReverseTranscoderIntegrationTest, ErrorFromBackend) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/bookstore.Bookstore/CreateBook"},
                                                  {"content-type", "application/grpc"}});

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  bookstore::CreateBookRequest create_book;
  create_book.set_shelf(12345);
  create_book.mutable_book()->set_id(123);
  auto request_data = Grpc::Common::serializeToGrpcFrame(create_book);
  codec_client_->sendData(*request_encoder_, *request_data, true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Method, Http::Headers::get().MethodValues.Put));
  EXPECT_THAT(upstream_request_->headers(),
              ContainsHeader(Http::Headers::get().Path, "/shelves/12345/books"));

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(400);
  response_headers.setContentType(Http::Headers::get().ContentTypeValues.Text);

  upstream_request_->encodeHeaders(response_headers, false);

  std::string response_str = "The request is missing the required book details";
  Buffer::OwnedImpl response_data;
  response_data.add(response_str);
  upstream_request_->encodeData(response_data, true);

  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  ASSERT_TRUE(response->trailers());
  EXPECT_THAT(*response->trailers(),
              ContainsHeader(Http::Headers::get().GrpcStatus,
                             std::to_string(Grpc::Status::WellKnownGrpcStatus::Internal)));
  EXPECT_THAT(*response->trailers(),
              ContainsHeader(Http::Headers::get().GrpcMessage, response_str));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
