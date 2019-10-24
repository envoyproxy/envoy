#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/runtime/runtime_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {
std::string createHeaderFragment(int num_headers) {
  // Create a header field with num_headers headers.
  std::string headers;
  for (int i = 0; i < num_headers; i++) {
    headers += "header" + std::to_string(i) + ": " + "\r\n";
  }
  return headers;
}
} // namespace

class Http1ServerConnectionImplTest : public testing::Test {
public:
  void initialize() {
    codec_ =
        std::make_unique<ServerConnectionImpl>(connection_, store_, callbacks_, codec_settings_,
                                               max_request_headers_kb_, max_request_headers_count_);
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks_;
  NiceMock<Http1Settings> codec_settings_;
  Http::ServerConnectionPtr codec_;

  void expectHeadersTest(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer,
                         TestHeaderMapImpl& expected_headers);
  void expect400(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer);
  void testRequestHeadersExceedLimit(std::string header_string);
  void testRequestHeadersAccepted(std::string header_string);

protected:
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  Stats::IsolatedStoreImpl store_;
};

void Http1ServerConnectionImplTest::expect400(Protocol p, bool allow_absolute_url,
                                              Buffer::OwnedImpl& buffer) {
  InSequence sequence;

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  if (allow_absolute_url) {
    codec_settings_.allow_absolute_url_ = allow_absolute_url;
    codec_ =
        std::make_unique<ServerConnectionImpl>(connection_, store_, callbacks_, codec_settings_,
                                               max_request_headers_kb_, max_request_headers_count_);
  }

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  EXPECT_THROW(codec_->dispatch(buffer), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
  EXPECT_EQ(p, codec_->protocol());
}

void Http1ServerConnectionImplTest::expectHeadersTest(Protocol p, bool allow_absolute_url,
                                                      Buffer::OwnedImpl& buffer,
                                                      TestHeaderMapImpl& expected_headers) {
  InSequence sequence;

  // Make a new 'codec' with the right settings
  if (allow_absolute_url) {
    codec_settings_.allow_absolute_url_ = allow_absolute_url;
    codec_ =
        std::make_unique<ServerConnectionImpl>(connection_, store_, callbacks_, codec_settings_,
                                               max_request_headers_kb_, max_request_headers_count_);
  }

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(p, codec_->protocol());
}

void Http1ServerConnectionImplTest::testRequestHeadersExceedLimit(std::string header_string) {
  initialize();

  std::string exception_reason;
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  codec_->dispatch(buffer);
  buffer = Buffer::OwnedImpl(header_string + "\r\n");
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), EnvoyException, "headers size exceeds limit");
}

void Http1ServerConnectionImplTest::testRequestHeadersAccepted(std::string header_string) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  codec_->dispatch(buffer);
  buffer = Buffer::OwnedImpl(header_string + "\r\n");
  codec_->dispatch(buffer);
}

TEST_F(Http1ServerConnectionImplTest, EmptyHeader) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{
      {"Test", ""},
      {"Hello", "World"},
      {":path", "/"},
      {":method", "GET"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nTest:\r\nHello: World\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, Http10) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(Protocol::Http10, codec_->protocol());
}

TEST_F(Http1ServerConnectionImplTest, Http10AbsoluteNoOp) {
  initialize();

  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http10Absolute) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/foobar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foobar HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http10MultipleResponses) {
  initialize();

  Http::MockStreamDecoder decoder;
  // Send a full HTTP/1.0 request and proxy a response.
  {
    Buffer::OwnedImpl buffer(
        "GET /foobar HTTP/1.0\r\nHost: www.somewhere.com\r\nconnection: keep-alive\r\n\r\n");
    Http::StreamEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));

    EXPECT_CALL(decoder, decodeHeaders_(_, true)).Times(1);
    codec_->dispatch(buffer);

    std::string output;
    ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));
    TestHeaderMapImpl headers{{":status", "200"}};
    response_encoder->encodeHeaders(headers, true);
    EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
    EXPECT_EQ(Protocol::Http10, codec_->protocol());
  }

  // Now send an HTTP/1.1 request and make sure the protocol is tracked correctly.
  {
    TestHeaderMapImpl expected_headers{
        {":authority", "www.somewhere.com"}, {":path", "/foobar"}, {":method", "GET"}};
    Buffer::OwnedImpl buffer("GET /foobar HTTP/1.1\r\nHost: www.somewhere.com\r\n\r\n");

    Http::StreamEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));
    EXPECT_CALL(decoder, decodeHeaders_(_, true)).Times(1);
    codec_->dispatch(buffer);
    EXPECT_EQ(Protocol::Http11, codec_->protocol());
  }
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath1) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath2) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathWithPort) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com:4532"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer(
      "GET http://www.somewhere.com:4532/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsoluteEnabledNoOp) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET /foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11InvalidRequest) {
  initialize();

  // Invalid because www.somewhere.com is not an absolute path nor an absolute url
  Buffer::OwnedImpl buffer("GET www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathNoSlash) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathBad) {
  initialize();

  Buffer::OwnedImpl buffer("GET * HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePortTooLarge) {
  initialize();

  Buffer::OwnedImpl buffer("GET http://foobar.com:1000000 HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11RelativeOnly) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "http://www.somewhere.com/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, false, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11Options) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "*"}, {":method", "OPTIONS"}};
  Buffer::OwnedImpl buffer("OPTIONS * HTTP/1.1\r\nHost: www.somewhere.com\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, SimpleGet) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, BadRequestNoStream) {
  initialize();

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  Buffer::OwnedImpl buffer("bad");
  EXPECT_THROW(codec_->dispatch(buffer), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, BadRequestStartedStream) {
  initialize();

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("G");
  codec_->dispatch(buffer);

  Buffer::OwnedImpl buffer2("g");
  EXPECT_THROW(codec_->dispatch(buffer2), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HostHeaderTranslation) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":authority", "hello"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHOST: hello\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

// Ensures that requests with invalid HTTP header values are not rejected
// when the runtime guard is not enabled for the feature.
TEST_F(Http1ServerConnectionImplTest, HeaderInvalidCharsRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  // When the runtime-guarded feature is NOT enabled, invalid header values
  // should be accepted by the codec.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.strict_header_validation", "false"}});

  initialize();

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer(
      absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: ", std::string(1, 3), "\r\n"));
  codec_->dispatch(buffer);
}

// Ensures that requests with invalid HTTP header values are properly rejected
// when the runtime guard is enabled for the feature.
TEST_F(Http1ServerConnectionImplTest, HeaderInvalidCharsRejection) {
  TestScopedRuntime scoped_runtime;
  // When the runtime-guarded feature is enabled, invalid header values
  // should result in a rejection.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.strict_header_validation", "true"}});

  initialize();

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer(
      absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: ", std::string(1, 3), "\r\n"));
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), CodecProtocolException,
                            "http/1.1 protocol error: header value contains invalid chars");
}

// Regression test for http-parser allowing embedded NULs in header values,
// verify we reject them.
TEST_F(Http1ServerConnectionImplTest, HeaderEmbeddedNulRejection) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer(
      absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: bar", std::string(1, '\0'), "baz\r\n"));
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), CodecProtocolException,
                            "http/1.1 protocol error: header value contains NUL");
}

// Mutate an HTTP GET with embedded NULs, this should always be rejected in some
// way (not necessarily with "head value contains NUL" though).
TEST_F(Http1ServerConnectionImplTest, HeaderMutateEmbeddedNul) {
  const std::string example_input = "GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: barbaz\r\n";

  for (size_t n = 1; n < example_input.size(); ++n) {
    initialize();

    InSequence sequence;

    Http::MockStreamDecoder decoder;
    EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

    Buffer::OwnedImpl buffer(
        absl::StrCat(example_input.substr(0, n), std::string(1, '\0'), example_input.substr(n)));
    EXPECT_THROW_WITH_REGEX(codec_->dispatch(buffer), CodecProtocolException,
                            "http/1.1 protocol error:");
  }
}

// Mutate an HTTP GET with CR or LF. These can cause an exception or maybe
// result in a valid decodeHeaders(). In any case, the validHeaderString()
// ASSERTs should validate we never have any embedded CR or LF.
TEST_F(Http1ServerConnectionImplTest, HeaderMutateEmbeddedCRLF) {
  const std::string example_input = "GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: barbaz\r\n";

  for (const char c : {'\r', '\n'}) {
    for (size_t n = 1; n < example_input.size(); ++n) {
      initialize();

      InSequence sequence;

      NiceMock<Http::MockStreamDecoder> decoder;
      EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

      Buffer::OwnedImpl buffer(
          absl::StrCat(example_input.substr(0, n), std::string(1, c), example_input.substr(n)));
      try {
        codec_->dispatch(buffer);
      } catch (CodecProtocolException&) {
      }
    }
  }
}

TEST_F(Http1ServerConnectionImplTest, CloseDuringHeadersComplete) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{"content-length", "5"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMapPtr&, bool) -> void {
        connection_.state_ = Network::Connection::State::Closing;
      }));
  EXPECT_CALL(decoder, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345");
  codec_->dispatch(buffer);
  EXPECT_NE(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, PostWithContentLength) {
  initialize();

  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{"content-length", "5"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false)).Times(1);

  Buffer::OwnedImpl expected_data1("12345");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false)).Times(1);

  Buffer::OwnedImpl expected_data2;
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), true)).Times(1);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseTrainProperHeaders) {
  codec_settings_.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}, {"some-header", "foo"}, {"some#header", "baz"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\nSome-Header: foo\r\nSome#Header: baz\r\nContent-Length: 0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseWith204) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "204"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 204 No Content\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseWith100Then200) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  response_encoder->encode100ContinueHeaders(continue_headers);
  EXPECT_EQ("HTTP/1.1 100 Continue\r\n\r\n", output);
  output.clear();

  // Test the special case where we encode 100 headers (no content length may be
  // appended) then 200 headers (content length 0 will be appended).
  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, MetadataTest) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  MetadataMap metadata_map = {{"key", "value"}};
  MetadataMapPtr metadata_map_ptr = std::make_unique<MetadataMap>(metadata_map);
  MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  response_encoder->encodeMetadata(metadata_map_vector);
  EXPECT_EQ(1, store_.counter("http1.metadata_not_supported_error").value());
}

TEST_F(Http1ServerConnectionImplTest, ChunkedResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, ContentLengthResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}, {"content-length", "11"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 11\r\n\r\nHello World", output);
}

TEST_F(Http1ServerConnectionImplTest, HeadRequestResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("HEAD / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}, {"content-length", "5"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HeadChunkedRequestResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("HEAD / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, DoubleRequest) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  std::string request("GET / HTTP/1.1\r\n\r\n");
  Buffer::OwnedImpl buffer(request);
  buffer.add(request);

  codec_->dispatch(buffer);
  EXPECT_EQ(request.size(), buffer.length());

  response_encoder->encodeHeaders(TestHeaderMapImpl{{":status", "200"}}, true);

  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, RequestWithTrailers) {
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
                           "World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, IgnoreUpgradeH2c) {
  initialize();

  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer(
      "GET http://www.somewhere.com/ HTTP/1.1\r\nConnection: "
      "Upgrade, HTTP2-Settings\r\nUpgrade: h2c\r\nHTTP2-Settings: token64\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, IgnoreUpgradeH2cClose) {
  initialize();

  TestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                     {":path", "/"},
                                     {":method", "GET"},
                                     {"connection", "Close"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nConnection: "
                           "Upgrade, Close, HTTP2-Settings\r\nUpgrade: h2c\r\nHTTP2-Settings: "
                           "token64\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, IgnoreUpgradeH2cCloseEtc) {
  initialize();

  TestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                     {":path", "/"},
                                     {":method", "GET"},
                                     {"connection", "Close,Etc"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nConnection: "
                           "Upgrade, Close, HTTP2-Settings, Etc\r\nUpgrade: h2c\r\nHTTP2-Settings: "
                           "token64\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequest) {
  initialize();

  InSequence sequence;
  NiceMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  EXPECT_CALL(decoder, decodeHeaders_(_, false)).Times(1);
  Buffer::OwnedImpl buffer(
      "POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: foo\r\ncontent-length:5\r\n\r\n");
  codec_->dispatch(buffer);

  Buffer::OwnedImpl expected_data1("12345");
  Buffer::OwnedImpl body("12345");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false)).Times(1);
  codec_->dispatch(body);

  Buffer::OwnedImpl expected_data2("abcd");
  Buffer::OwnedImpl websocket_payload("abcd");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), false)).Times(1);
  codec_->dispatch(websocket_payload);
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithEarlyData) {
  initialize();

  InSequence sequence;
  NiceMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false)).Times(1);
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false)).Times(1);
  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: "
                           "foo\r\ncontent-length:5\r\n\r\n12345abcd");
  codec_->dispatch(buffer);
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithTEChunked) {
  initialize();

  InSequence sequence;
  NiceMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Even with T-E chunked, the data should neither be inspected for (the not
  // present in this unit test) chunks, but simply passed through.
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false)).Times(1);
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false)).Times(1);
  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: "
                           "foo\r\ntransfer-encoding: chunked\r\n\r\n12345abcd");
  codec_->dispatch(buffer);
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithNoBody) {
  initialize();

  InSequence sequence;
  NiceMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Make sure we avoid the deferred_end_stream_headers_ optimization for
  // requests-with-no-body.
  Buffer::OwnedImpl expected_data("abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false)).Times(1);
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false)).Times(1);
  Buffer::OwnedImpl buffer(
      "GET / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: foo\r\ncontent-length: 0\r\n\r\nabcd");
  codec_->dispatch(buffer);
}

TEST_F(Http1ServerConnectionImplTest, WatermarkTest) {
  EXPECT_CALL(connection_, bufferLimit()).Times(1).WillOnce(Return(10));
  initialize();

  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);

  Http::MockStreamCallbacks stream_callbacks;
  response_encoder->getStream().addCallbacks(stream_callbacks);

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ServerConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  // Fake out the underlying Network::Connection buffer being drained.
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  static_cast<ServerConnection*>(codec_.get())
      ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

class Http1ClientConnectionImplTest : public testing::Test {
public:
  void initialize() {
    codec_ = std::make_unique<ClientConnectionImpl>(connection_, store_, callbacks_,
                                                    max_response_headers_count_);
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockConnectionCallbacks> callbacks_;
  std::unique_ptr<ClientConnectionImpl> codec_;

protected:
  Stats::IsolatedStoreImpl store_;
  uint32_t max_response_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
};

TEST_F(Http1ClientConnectionImplTest, SimpleGet) {
  initialize();

  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, HostHeaderTranslate) {
  initialize();

  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, Reset) {
  initialize();

  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::LocalReset, _));
  request_encoder.getStream().resetStream(StreamResetReason::LocalReset);
}

// Verify that we correctly enable reads on the connection when the final pipeline response is
// received.
TEST_F(Http1ClientConnectionImplTest, FlowControlReadDisabledReenable) {
  initialize();

  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder* request_encoder = &codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  // 1st pipeline request.
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
  output.clear();

  // 2nd pipeline request.
  request_encoder = &codec_->newStream(response_decoder);
  request_encoder->encodeHeaders(headers, false);
  Buffer::OwnedImpl empty;
  request_encoder->encodeData(empty, true);
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n", output);

  // 1st response.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
  codec_->dispatch(response);

  // Simulate the underlying connection being backed up. Ensure that it is
  // read-enabled when the final response completes.
  EXPECT_CALL(connection_, readEnabled())
      .Times(2)
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, readDisable(false));

  // 2nd response.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response2("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
  codec_->dispatch(response2);
}

TEST_F(Http1ClientConnectionImplTest, PrematureResponse) {
  initialize();

  Buffer::OwnedImpl response("HTTP/1.1 408 Request Timeout\r\nConnection: Close\r\n\r\n");
  EXPECT_THROW(codec_->dispatch(response), PrematureResponseException);
}

TEST_F(Http1ClientConnectionImplTest, EmptyBodyResponse503) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, EmptyBodyResponse200) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, HeadRequest) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "HEAD"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, 204Response) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, 100Response) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decode100ContinueHeaders_(_));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl initial_response("HTTP/1.1 100 Continue\r\n\r\n");
  codec_->dispatch(initial_response);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, BadEncodeParams) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;

  // Need to set :method and :path
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  EXPECT_THROW(request_encoder.encodeHeaders(TestHeaderMapImpl{{":path", "/"}}, true),
               CodecClientException);
  EXPECT_THROW(request_encoder.encodeHeaders(TestHeaderMapImpl{{":method", "GET"}}, true),
               CodecClientException);
}

TEST_F(Http1ClientConnectionImplTest, NoContentLengthResponse) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl expected_data1("Hello World");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data1), false));

  Buffer::OwnedImpl expected_data2;
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data2), true));

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\nHello World");
  codec_->dispatch(response);

  Buffer::OwnedImpl empty;
  codec_->dispatch(empty);
}

TEST_F(Http1ClientConnectionImplTest, ResponseWithTrailers) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
                             "World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");
  codec_->dispatch(response);
  EXPECT_EQ(0UL, response.length());
}

TEST_F(Http1ClientConnectionImplTest, GiantPath) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/" + std::string(16384, 'a')}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, UpgradeResponse) {
  initialize();

  InSequence s;

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  // Send upgrade headers
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response(
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: upgrade\r\nUpgrade: websocket\r\n\r\n");
  codec_->dispatch(response);

  // Send body payload
  Buffer::OwnedImpl expected_data1("12345");
  Buffer::OwnedImpl body("12345");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data1), false)).Times(1);
  codec_->dispatch(body);

  // Send websocket payload
  Buffer::OwnedImpl expected_data2("abcd");
  Buffer::OwnedImpl websocket_payload("abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data2), false)).Times(1);
  codec_->dispatch(websocket_payload);
}

// Same data as above, but make sure directDispatch immediately hands off any
// outstanding data.
TEST_F(Http1ClientConnectionImplTest, UpgradeResponseWithEarlyData) {
  initialize();

  InSequence s;

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  // Send upgrade headers
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data), false)).Times(1);
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: "
                             "upgrade\r\nUpgrade: websocket\r\n\r\n12345abcd");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, WatermarkTest) {
  EXPECT_CALL(connection_, bufferLimit()).Times(1).WillOnce(Return(10));
  initialize();

  InSequence s;

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  Http::MockStreamCallbacks stream_callbacks;
  request_encoder.getStream().addCallbacks(stream_callbacks);

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  // Do a large write. This will result in the buffer temporarily going over the
  // high watermark and then draining.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  // Fake out the underlying Network::Connection buffer being drained.
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/3589. Upstream sends multiple
// responses to the same request. The request causes the write buffer to go above high
// watermark. When the 2nd response is received, we throw a premature response exception, and the
// caller attempts to close the connection. This causes the network connection to attempt to write
// pending data, even in the no flush scenario, which can cause us to go below low watermark
// which then raises callbacks for a stream that no longer exists.
TEST_F(Http1ClientConnectionImplTest, HighwatermarkMultipleResponses) {
  initialize();

  InSequence s;

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  Http::MockStreamCallbacks stream_callbacks;
  request_encoder.getStream().addCallbacks(stream_callbacks);

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  codec_->dispatch(response);

  Buffer::OwnedImpl response2("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
  EXPECT_THROW(codec_->dispatch(response2), PrematureResponseException);

  // Fake a call for going below the low watermark. Make sure no stream callbacks get called.
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}
TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersRejected) {
  // Default limit of 60 KiB
  std::string long_string = "big: " + std::string(60 * 1024, 'q') + "\r\n";
  testRequestHeadersExceedLimit(long_string);
}

// Tests that the default limit for the number of request headers is 100.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersRejected) {
  // Send a request with 101 headers.
  testRequestHeadersExceedLimit(createHeaderFragment(101));
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersSplitRejected) {
  // Default limit of 60 KiB
  initialize();

  std::string exception_reason;
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  codec_->dispatch(buffer);

  std::string long_string = std::string(1024, 'q');
  for (int i = 0; i < 59; i++) {
    buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
    codec_->dispatch(buffer);
  }
  // the 60th 1kb header should induce overflow
  buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), EnvoyException, "headers size exceeds limit");
}

// Tests that the 101th request header causes overflow with the default max number of request
// headers.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersSplitRejected) {
  // Default limit of 100.
  initialize();

  std::string exception_reason;
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder, bool) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  codec_->dispatch(buffer);

  // Dispatch 100 headers.
  buffer = Buffer::OwnedImpl(createHeaderFragment(100));
  codec_->dispatch(buffer);

  // The final 101th header should induce overflow.
  buffer = Buffer::OwnedImpl("header101:\r\n\r\n");
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), EnvoyException, "headers size exceeds limit");
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersAccepted) {
  max_request_headers_kb_ = 65;
  std::string long_string = "big: " + std::string(64 * 1024, 'q') + "\r\n";
  testRequestHeadersAccepted(long_string);
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersAcceptedMaxConfigurable) {
  max_request_headers_kb_ = 96;
  std::string long_string = "big: " + std::string(95 * 1024, 'q') + "\r\n";
  testRequestHeadersAccepted(long_string);
}

// Tests that the number of request headers is configurable.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersAccepted) {
  max_request_headers_count_ = 150;
  // Create a request with 150 headers.
  testRequestHeadersAccepted(createHeaderFragment(150));
}

// Tests that response headers of 80 kB fails.
TEST_F(Http1ClientConnectionImplTest, LargeResponseHeadersRejected) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  codec_->dispatch(buffer);
  std::string long_header = "big: " + std::string(80 * 1024, 'q') + "\r\n";
  buffer = Buffer::OwnedImpl(long_header);
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), EnvoyException, "headers size exceeds limit");
}

// Tests that the size of response headers for HTTP/1 must be under 80 kB.
TEST_F(Http1ClientConnectionImplTest, LargeResponseHeadersAccepted) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  codec_->dispatch(buffer);
  std::string long_header = "big: " + std::string(79 * 1024, 'q') + "\r\n";
  buffer = Buffer::OwnedImpl(long_header);
  codec_->dispatch(buffer);
}

// Exception called when the number of response headers exceeds the default value of 100.
TEST_F(Http1ClientConnectionImplTest, ManyResponseHeadersRejected) {
  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  codec_->dispatch(buffer);
  buffer = Buffer::OwnedImpl(createHeaderFragment(101) + "\r\n");
  EXPECT_THROW_WITH_MESSAGE(codec_->dispatch(buffer), EnvoyException, "headers size exceeds limit");
}

// Tests that the number of response headers is configurable.
TEST_F(Http1ClientConnectionImplTest, ManyResponseHeadersAccepted) {
  max_response_headers_count_ = 152;

  initialize();

  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  codec_->dispatch(buffer);
  // Response already contains one header.
  buffer = Buffer::OwnedImpl(createHeaderFragment(150) + "\r\n");
  codec_->dispatch(buffer);
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
