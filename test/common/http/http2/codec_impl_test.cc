#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/stats/stats_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
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

TEST_P(Http2CodecImplTest, ExpectContinueHeadersOnlyResponse) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  TestHeaderMapImpl request_headers;
  request_headers.addViaCopy("expect", "100-continue");
  HttpTestUtility::addDefaultHeaders(request_headers);
  TestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(HeaderMapEqual(&continue_headers), false));
  request_encoder.encodeHeaders(request_headers, false);

  EXPECT_CALL(request_decoder, decodeData(_, true));
  Buffer::OwnedImpl hello("hello");
  request_encoder.encodeData(hello, true);

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(HeaderMapEqual(&response_headers), true));
  response_encoder->encodeHeaders(response_headers, true);
}

TEST_P(Http2CodecImplTest, ExpectContinueTrailersResponse) {
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);

  MockStreamDecoder request_decoder;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        return request_decoder;
      }));

  TestHeaderMapImpl request_headers;
  request_headers.addViaCopy("expect", "100-continue");
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));

  TestHeaderMapImpl continue_headers{{":status", "100"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(HeaderMapEqual(&continue_headers), false));
  request_encoder.encodeHeaders(request_headers, false);

  EXPECT_CALL(request_decoder, decodeData(_, true));
  Buffer::OwnedImpl hello("hello");
  request_encoder.encodeData(hello, true);

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(HeaderMapEqual(&response_headers), false));
  response_encoder->encodeHeaders(response_headers, false);

  TestHeaderMapImpl response_trailers{{"foo", "bar"}};
  EXPECT_CALL(response_decoder, decodeTrailers_(HeaderMapEqual(&response_trailers)));
  response_encoder->encodeTrailers(response_trailers);
}

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

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, true));
  request_encoder.encodeHeaders(request_headers, true);

  EXPECT_CALL(client_callbacks_, onGoAway());
  server_.shutdownNotice();
  server_.goAway();

  TestHeaderMapImpl response_headers{{":status", "200"}};
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

  TestHeaderMapImpl request_headers;
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
  request_encoder.encodeHeaders(TestHeaderMapImpl{}, true);
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

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  request_encoder.encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder, decodeData(_, false));
  Buffer::OwnedImpl hello("hello");
  request_encoder.encodeData(hello, false);
  EXPECT_CALL(request_decoder, decodeTrailers_(_));
  request_encoder.encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  response_encoder->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder->encodeData(world, false);
  EXPECT_CALL(response_decoder, decodeTrailers_(_));
  response_encoder->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});
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

  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  request_encoder.encodeHeaders(request_headers, false);
  EXPECT_CALL(request_decoder, decodeData(_, false)).Times(AtLeast(1));
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  request_encoder.encodeData(body, false);
  EXPECT_CALL(request_decoder, decodeTrailers_(_));
  request_encoder.encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});

  // Flush pending data.
  setupDefaultConnectionMocks();
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);

  TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  response_encoder->encodeHeaders(response_headers, false);
  EXPECT_CALL(response_decoder, decodeData(_, false));
  Buffer::OwnedImpl world("world");
  response_encoder->encodeData(world, false);
  EXPECT_CALL(response_decoder, decodeTrailers_(_));
  response_encoder->encodeTrailers(TestHeaderMapImpl{{"trailing", "header"}});
}

TEST_P(Http2CodecImplTest, DeferredReset) {
  InSequence s;

  // Buffer server data so we can make sure we don't get any window updates.
  ON_CALL(client_connection_, write(_))
      .WillByDefault(
          Invoke([&](Buffer::Instance& data) -> void { server_wrapper_.buffer_.add(data); }));

  // This will send a request that is bigger than the initial window size. When we then reset it,
  // after attempting to flush we should reset the stream and drop the data we could not send.
  MockStreamDecoder response_decoder;
  StreamEncoder& request_encoder = client_.newStream(response_decoder);
  TestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  request_encoder.encodeHeaders(request_headers, false);
  Buffer::OwnedImpl body(std::string(1024 * 1024, 'a'));
  request_encoder.encodeData(body, true);
  request_encoder.getStream().resetStream(StreamResetReason::LocalReset);

  MockStreamDecoder request_decoder;
  MockStreamCallbacks callbacks;
  StreamEncoder* response_encoder;
  EXPECT_CALL(server_callbacks_, newStream(_))
      .WillOnce(Invoke([&](StreamEncoder& encoder) -> StreamDecoder& {
        response_encoder = &encoder;
        encoder.getStream().addCallbacks(callbacks);
        return request_decoder;
      }));
  EXPECT_CALL(request_decoder, decodeHeaders_(_, false));
  EXPECT_CALL(request_decoder, decodeData(_, false)).Times(AtLeast(1));
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::RemoteReset));
  server_wrapper_.dispatch(Buffer::OwnedImpl(), server_);
}

INSTANTIATE_TEST_CASE_P(Http2CodecImplTest, Http2CodecImplTest,
                        testing::Values(0, CodecOptions::NoCompression));

TEST(Http2CodecUtility, reconstituteCrumbledCookies) {
  {
    HeaderString key;
    HeaderString value;
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().ContentLength);
    HeaderString value;
    value.setInteger(5);
    HeaderString cookies;
    EXPECT_FALSE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_TRUE(cookies.empty());
  }

  {
    HeaderString key(Headers::get().Cookie);
    HeaderString value;
    value.setCopy("a=b", 3);
    HeaderString cookies;
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key, value, cookies));
    EXPECT_EQ(cookies, "a=b");

    HeaderString key2(Headers::get().Cookie);
    HeaderString value2;
    value2.setCopy("c=d", 3);
    EXPECT_TRUE(Utility::reconstituteCrumbledCookies(key2, value2, cookies));
    EXPECT_EQ(cookies, "a=b; c=d");
  }
}

} // Http2
} // Http
