#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Http {
namespace Http1 {

class Http1ServerConnectionImplTest : public ::testing::Test {
public:
  Http1ServerConnectionImplTest() : codec_(new ServerConnectionImpl(connection_, callbacks_)) {}

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks_;
  Http::ServerConnectionPtr codec_;
};

TEST_F(Http1ServerConnectionImplTest, EmptyHeader) {
  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{
      {"Test", ""},
      {"Hello", "World"},
      {":version", "HTTP/1.1"},
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

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":version", "HTTP/1.0"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, SimpleGet) {
  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{{":version", "HTTP/1.1"}, {":path", "/"}, {":method", "GET"}};
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

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("G");
  codec_->dispatch(buffer);

  Buffer::OwnedImpl buffer2("g");
  EXPECT_THROW(codec_->dispatch(buffer2), CodecProtocolException);
  EXPECT_EQ("HTTP/1.1 400 Bad Request\r\ncontent-length: 0\r\nconnection: close\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HostHeaderTranslation) {
  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{
      {":authority", "hello"}, {":version", "HTTP/1.1"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true)).Times(1);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHOST: hello\r\n\r\n");
  codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, CloseDuringHeadersComplete) {
  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{
      {"content-length", "5"}, {":version", "HTTP/1.1"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMapPtr&, bool)
                           -> void { connection_.state_ = Network::Connection::State::Closing; }));
  EXPECT_CALL(decoder, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345");
  codec_->dispatch(buffer);
  EXPECT_NE(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, PostWithContentLength) {
  InSequence sequence;

  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_)).WillOnce(ReturnRef(decoder));

  TestHeaderMapImpl expected_headers{
      {"content-length", "5"}, {":version", "HTTP/1.1"}, {":path", "/"}, {":method", "POST"}};
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
      {"content-length", "100"}, {":version", "HTTP/1.1"}, {":path", "/"}, {":method", "POST"}};
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
  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, HostHeaderTranslate) {
  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_)).WillByDefault(AddBufferToString(&output));

  TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  request_encoder.encodeHeaders(headers, true);
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, Reset) {
  Http::MockStreamDecoder response_decoder;
  Http::StreamEncoder& request_encoder = codec_->newStream(response_decoder);

  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::LocalReset));
  request_encoder.getStream().resetStream(StreamResetReason::LocalReset);
}

TEST_F(Http1ClientConnectionImplTest, MultipleHeaderOnlyThenNoContentLength) {
  Http::MockStreamDecoder response_decoder;
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

} // Http1
} // Http
