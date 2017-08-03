#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;
using testing::StrictMock;
using testing::_;

namespace Http {
namespace Http1 {

class Http1ServerConnectionImplTest : public ::testing::Test {
public:
  Http1ServerConnectionImplTest()
      : codec_(new ServerConnectionImpl(connection_, callbacks_, codec_settings_)) {}

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks_;
  NiceMock<Http1Settings> codec_settings_;
  Http::ServerConnectionPtr codec_;

  void expectHeadersTest(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer,
                         TestHeaderMapImpl& expected_headers);
  void expect400(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer);
};

void Http1ServerConnectionImplTest::expect400(Protocol p, bool allow_absolute_url,
                                              Buffer::OwnedImpl& buffer) {
  InSequence sequence;

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  if (allow_absolute_url) {
    codec_settings_.allow_absolute_url_ = allow_absolute_url;
    codec_.reset(new ServerConnectionImpl(connection_, callbacks_, codec_settings_));
  }

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

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
    codec_.reset(new ServerConnectionImpl(connection_, callbacks_, codec_settings_));
  }

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(p, codec_->protocol());
}

TEST_F(Http1ServerConnectionImplTest, EmptyHeader) {
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

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
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(Protocol::Http10, codec_->protocol());
}

TEST_F(Http1ServerConnectionImplTest, Http10AbsoluteNoOp) {
  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http10Absolute) {
  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/foobar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foobar HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath1) {
  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath2) {
  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsoluteEnabledNoOp) {
  TestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET /foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11InvalidRequest) {
  // Invalid because www.somewhere.com is not an absolute path nor an absolute url
  Buffer::OwnedImpl buffer("GET www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathNoSlash) {
  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathBad) {
  Buffer::OwnedImpl buffer("GET * HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePortTooLarge) {
  Buffer::OwnedImpl buffer("GET http://foobar.com:1000000 HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, Http11RelativeOnly) {
  TestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "http://www.somewhere.com/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, false, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11Options) {
  TestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "*"}, {":method", "OPTIONS"}};
  Buffer::OwnedImpl buffer("OPTIONS * HTTP/1.1\r\nHost: www.somewhere.com\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, SimpleGet) {
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, BadRequestNoStream) {
  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  Buffer::OwnedImpl buffer("bad");
  EXPECT_THROW(codec_->dispatch(buffer), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, BadRequestStartedStream) {
  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("G");
  codec_->dispatch(buffer);

  Buffer::OwnedImpl buffer2("g");
  EXPECT_THROW(codec_->dispatch(buffer2), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HostHeaderTranslation) {
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":authority", "hello"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHOST: hello\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, CloseDuringHeadersComplete) {
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

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
  InSequence sequence;

  StrictMock<Http::MockStreamDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

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
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, ChunkedResponse) {
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, ContentLengthResponse) {
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}, {"content-length", "11"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 11\r\n\r\nHello World", output);
}

TEST_F(Http1ServerConnectionImplTest, HeadRequestResponse) {
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("HEAD / HTTP/1.1\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":status", "200"}, {"content-length", "5"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, ExpectContinueResponse) {
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl expected_headers{
      {"content-length", "100"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false)).Times(1);

  Buffer::OwnedImpl buffer(
      "POST / HTTP/1.1\r\nExpect: 100-continue\r\ncontent-length: 100\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ("HTTP/1.1 100 Continue\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, DoubleRequest) {
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
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
  NiceMock<Http::MockStreamDecoder> decoder;
  Http::StreamEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_))
      .WillOnce(Invoke([&](Http::StreamEncoder& encoder) -> Http::StreamDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
                           "World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

class Http1ClientConnectionImplTest : public testing::Test {
public:
  Http1ClientConnectionImplTest() : codec_(new ClientConnectionImpl(connection_, callbacks_)) {}

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockConnectionCallbacks> callbacks_;
  Http::ClientConnectionPtr codec_;
};

TEST_F(Http1ClientConnectionImplTest, SimpleGet) {
  StrictMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, HostHeaderTranslate) {
  StrictMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, Reset) {
  StrictMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  StrictMock<Http::MockStreamCallbacks> callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::LocalReset));
  request_encoder.getStream().resetStream(StreamResetReason::LocalReset);
}

TEST_F(Http1ClientConnectionImplTest, MultipleHeaderOnlyThenNoContentLength) {
  StrictMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder* request_encoder = &codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder->encodeHeaders(headers, true);

  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
  output.clear();

  request_encoder = &codec_->newStream(response_decoder);
  request_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl empty;
  request_encoder->encodeData(empty, true);

  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, PrematureResponse) {
  Buffer::OwnedImpl response("HTTP/1.1 408 Request Timeout\r\nConnection: Close\r\n\r\n");
  EXPECT_THROW(codec_->dispatch(response), PrematureResponseException);
}

TEST_F(Http1ClientConnectionImplTest, HeadRequest) {
  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "HEAD"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, 204Response) {
  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

TEST_F(Http1ClientConnectionImplTest, BadEncodeParams) {
  NiceMock<Http::MockStreamDecoder> response_decoder;

  // Need to set :method and :path
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  EXPECT_THROW(request_encoder.encodeHeaders(TestHeaderMapImpl{{":path", "/"}}, true),
               CodecClientException);
  EXPECT_THROW(request_encoder.encodeHeaders(TestHeaderMapImpl{{":method", "GET"}}, true),
               CodecClientException);
}

TEST_F(Http1ClientConnectionImplTest, NoContentLengthResponse) {
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
  NiceMock<Http::MockStreamDecoder> response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);
  TestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/" + std::string(16384, 'a')}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  codec_->dispatch(response);
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
