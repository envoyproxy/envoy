#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/stats/stats_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;

namespace Http {
namespace Http2 {

class Http2CodecImplTest : public testing::TestWithParam<uint64_t> {
public:
  struct ConnectionWrapper {
    void dispatch(const Buffer::Instance& data, ConnectionImpl& connection) {
      buffer_.add(data);
      if (!dispatching_) {
        while (buffer_.length() > 0) {
          dispatching_ = true;
          connection.dispatch(buffer_);
          dispatching_ = false;
        }
      }
    }

    bool dispatching_{};
    Buffer::OwnedImpl buffer_;
  };

  Http2CodecImplTest()
      : client_(client_connection_, client_callbacks_, stats_store_, GetParam()),
        server_(server_connection_, server_callbacks_, stats_store_, GetParam()) {
    setupDefaultConnectionMocks();
  }

  void setupDefaultConnectionMocks() {
    ON_CALL(client_connection_, write(_))
        .WillByDefault(Invoke([&](Buffer::Instance& data)
                                  -> void { server_wrapper_.dispatch(data, server_); }));
    ON_CALL(server_connection_, write(_))
        .WillByDefault(Invoke([&](Buffer::Instance& data)
                                  -> void { client_wrapper_.dispatch(data, client_); }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Network::MockConnection> client_connection_;
  MockConnectionCallbacks client_callbacks_;
  ClientConnectionImpl client_;
  ConnectionWrapper client_wrapper_;

  NiceMock<Network::MockConnection> server_connection_;
  MockServerConnectionCallbacks server_callbacks_;
  ServerConnectionImpl server_;
  ConnectionWrapper server_wrapper_;
};

TEST_P(Http2CodecImplTest, ShutdownNotice) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  HeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, true));
  request_encoder.encodeHeaders(request_headers, true);

  EXPECT_CALL(client_callbacks_, onGoAway());
  server_.shutdownNotice();
  server_.goAway();

  HeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  response_encoder->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, RefusedStreamReset) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  HeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  request_encoder.encodeHeaders(request_headers, false);

  MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteRefusedStreamReset));
  response_encoder->getStream().resetStream(StreamResetReason::LocalRefusedStreamReset);
}

TEST_P(Http2CodecImplTest, InvalidFrame) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  ON_CALL(client_connection_, write(_))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data) -> void { server_wrapper_.buffer_.add(data); }));
  request_encoder.encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_THROW(server_wrapper_.dispatch(Buffer::OwnedImpl(), server_), CodecProtocolException);
}

TEST_P(Http2CodecImplTest, TrailingHeaders) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  HeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  request_encoder.encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder, decodeData(_, false));
  request_encoder.encodeData(Buffer::OwnedImpl("hello"), false);
  EXPECT_CALL(request_decoder, decodeTrailers_(_));
  request_encoder.encodeTrailers(HeaderMapImpl{{"trailing", "header"}});

  HeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  response_encoder->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder, decodeData(_, false));
  response_encoder->encodeData(Buffer::OwnedImpl("world"), false);
  EXPECT_CALL(response_decoder, decodeTrailers_(_));
  response_encoder->encodeTrailers(HeaderMapImpl{{"trailing", "header"}});
}

TEST_P(Http2CodecImplTest, TrailingHeadersLargeBody) {
  // Buffer server data so we can make sure we don't get any window updates.
  ON_CALL(client_connection_, write(_))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data) -> void { server_wrapper_.buffer_.add(data); }));

  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  HeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  request_encoder.encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder, decodeData(_, false)).Times(AtLeast(1));
  request_encoder.encodeData(Buffer::OwnedImpl(std::string(1024 * 1024, 'a')), false);
  EXPECT_CALL(request_decoder, decodeTrailers_(_));
  request_encoder.encodeTrailers(HeaderMapImpl{{"trailing", "header"}});

  // Flush pending data.
  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);

  HeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  response_encoder->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder, decodeData(_, false));
  response_encoder->encodeData(Buffer::OwnedImpl("world"), false);
  EXPECT_CALL(response_decoder, decodeTrailers_(_));
  response_encoder->encodeTrailers(HeaderMapImpl{{"trailing", "header"}});
}

INSTANTIATE_TEST_CASE_P(Http2CodecImplTest, Http2CodecImplTest,
                        testing::Values(0, CodecOptions::NoCompression));

TEST(Http2CodecUtility, reconstituteCrumbledCookies) {
  {
    HeaderMapImpl headers;
    Utility::reconstituteCrumbledCookies(headers);
    EXPECT_EQ(headers, HeaderMapImpl{});
  }

  {
    HeaderMapImpl headers{{"foo", "bar"}, {"cookie", "a=b"}};
    Utility::reconstituteCrumbledCookies(headers);
    EXPECT_EQ(headers, (HeaderMapImpl{{"foo", "bar"}, {"cookie", "a=b"}}));
  }

  {
    HeaderMapImpl headers{{"foo", "bar"}, {"cookie", "a=b"}, {"cookie", "c=d"}};
    Utility::reconstituteCrumbledCookies(headers);
    EXPECT_EQ(headers, (HeaderMapImpl{{"foo", "bar"}, {"cookie", "a=b; c=d"}}));
  }

  {
    HeaderMapImpl headers{{"foo", "bar"},
                          {"cookie", "a=b"},
                          {"hello", "world"},
                          {"cookie", "c=d"},
                          {"more", "headers"},
                          {"cookie", "blah=blah"}};
    Utility::reconstituteCrumbledCookies(headers);
    EXPECT_EQ(headers, (HeaderMapImpl{{"foo", "bar"},
                                      {"hello", "world"},
                                      {"more", "headers"},
                                      {"cookie", "a=b; c=d; blah=blah"}}));
  }
}

} // Http2
} // Http
