#include "common/http/exception.h"

#include "test/common/http/common.h"
#include "test/common/http/http2/frame_replay.h"

#include "gtest/gtest.h"

#define EXPECT_NEXT_BYTES(istream, ...)                                                            \
  do {                                                                                             \
    std::vector<uint8_t> expected_bytes{__VA_ARGS__};                                              \
    std::vector<uint8_t> actual_bytes(expected_bytes.size());                                      \
    istream->read(reinterpret_cast<char*>(actual_bytes.data()), expected_bytes.size());            \
    EXPECT_EQ(actual_bytes, expected_bytes);                                                       \
  } while (0)

using testing::AnyNumber;
using testing::InvokeWithoutArgs;

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

// For organizational purposes only.
class RequestFrameCommentTest : public ::testing::Test {};
class ResponseFrameCommentTest : public ::testing::Test {};

// Creates and sets up a stream to reply to.
void setupStream(ClientCodecFrameInjector& codec, TestClientConnectionImpl& connection) {
  codec.request_encoder_ = &connection.newStream(codec.response_decoder_);
  codec.request_encoder_->getStream().addCallbacks(codec.client_stream_callbacks_);
  // Setup a single stream to inject frames as a reply to.
  TestRequestHeaderMapImpl request_headers;
  HttpTestUtility::addDefaultHeaders(request_headers);
  codec.request_encoder_->encodeHeaders(request_headers, true);
}

// Validate that a simple Huffman encoded request HEADERS frame can be decoded.
TEST_F(RequestFrameCommentTest, SimpleExampleHuffman) {
  FileFrame header{"request_header_corpus/simple_example_huffman"};

  // Validate HEADERS content matches intent.
  auto header_bytes = header.istream();
  // Payload size is 18 bytes.
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x00, 0x12);
  // HEADERS frame with END_STREAM | END_HEADERS for stream 1.
  EXPECT_NEXT_BYTES(header_bytes, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01);
  // Static table :scheme: http, :method: GET
  EXPECT_NEXT_BYTES(header_bytes, 0x86, 0x82);
  // Static table :authority, Huffman 'host'
  EXPECT_NEXT_BYTES(header_bytes, 0x41, 0x83, 0x9c, 0xe8, 0x4f);
  // Static table :path: /
  EXPECT_NEXT_BYTES(header_bytes, 0x84);
  // Huffman foo: barbaz
  EXPECT_NEXT_BYTES(header_bytes, 0x40, 0x82, 0x94, 0xe7, 0x85, 0x8c, 0x76, 0x46, 0x3f, 0x7f);

  // Validate HEADERS decode.
  ServerCodecFrameInjector codec;
  TestServerConnectionImpl connection(
      codec.server_connection_, codec.server_callbacks_, codec.stats_store_, codec.options_,
      Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
      envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  EXPECT_TRUE(codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  expected_headers.addCopy("foo", "barbaz");
  EXPECT_CALL(codec.request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), true));
  EXPECT_TRUE(codec.write(header.frame(), connection).ok());
}

// Validate that a simple Huffman encoded response HEADERS frame can be decoded.
TEST_F(ResponseFrameCommentTest, SimpleExampleHuffman) {
  FileFrame header{"response_header_corpus/simple_example_huffman"};

  // Validate HEADERS content matches intent.
  auto header_bytes = header.istream();

  // Payload size is 15 bytes.
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x00, 0x0f);
  // HEADERS frame with END_STREAM | END_HEADERS for stream 1.
  EXPECT_NEXT_BYTES(header_bytes, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01);
  // Static table :status: 200
  EXPECT_NEXT_BYTES(header_bytes, 0x88);
  // Huffman compression: test
  EXPECT_NEXT_BYTES(header_bytes, 0x40, 0x88, 0x21, 0xe9, 0xae, 0xc2, 0xa1, 0x06, 0x3d, 0x5f, 0x83,
                    0x49, 0x50, 0x9f);

  // Validate HEADERS decode.
  ClientCodecFrameInjector codec;
  TestClientConnectionImpl connection(
      codec.client_connection_, codec.client_callbacks_, codec.stats_store_, codec.options_,
      Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
      ProdNghttp2SessionFactory::get());
  setupStream(codec, connection);

  EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
  TestResponseHeaderMapImpl expected_headers;
  expected_headers.addCopy(":status", "200");
  expected_headers.addCopy("compression", "test");
  EXPECT_CALL(codec.response_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), true));
  EXPECT_TRUE(codec.write(header.frame(), connection).ok());
}

// Validate that a simple non-Huffman request HEADERS frame with no static table user either can be
// decoded.
TEST_F(RequestFrameCommentTest, SimpleExamplePlain) {
  FileFrame header{"request_header_corpus/simple_example_plain"};

  // Validate HEADERS content matches intent.
  auto header_bytes = header.istream();
  // Payload size is 65 bytes.
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x00, 0x41);
  // HEADERS frame with END_STREAM | END_HEADERS for stream 1.
  EXPECT_NEXT_BYTES(header_bytes, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01);
  // Literal unindexed :scheme: http
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x07, 0x3A, 0x73, 0x63, 0x68, 0x65, 0x6D, 0x65);
  EXPECT_NEXT_BYTES(header_bytes, 0x04, 0x68, 0x74, 0x74, 0x70);
  // Literal unindexed :method: GET
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x07, 0x3A, 0x6D, 0x65, 0x74, 0x68, 0x6F, 0x64);
  EXPECT_NEXT_BYTES(header_bytes, 0x03, 0x47, 0x45, 0x54);
  // Literal unindexed :authority: host
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x0A, 0x3A, 0x61, 0x75, 0x74, 0x68, 0x6F, 0x72, 0x69, 0x74,
                    0x79);
  EXPECT_NEXT_BYTES(header_bytes, 0x04, 0x68, 0x6F, 0x73, 0x74);
  // Literal unindexed :path: /
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x05, 0x3A, 0x70, 0x61, 0x74, 0x68);
  EXPECT_NEXT_BYTES(header_bytes, 0x01, 0x2F);
  // Literal unindexed foo: barbaz
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x03, 0x66, 0x6F, 0x6F);
  EXPECT_NEXT_BYTES(header_bytes, 0x06, 0x62, 0x61, 0x72, 0x62, 0x61, 0x7A);

  // Validate HEADERS decode.
  ServerCodecFrameInjector codec;
  TestServerConnectionImpl connection(
      codec.server_connection_, codec.server_callbacks_, codec.stats_store_, codec.options_,
      Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
      envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  EXPECT_TRUE(codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
  TestRequestHeaderMapImpl expected_headers;
  HttpTestUtility::addDefaultHeaders(expected_headers);
  expected_headers.addCopy("foo", "barbaz");
  EXPECT_CALL(codec.request_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), true));
  EXPECT_TRUE(codec.write(header.frame(), connection).ok());
}

// Validate that a simple non-Huffman response HEADERS frame with no static table user either can be
// decoded.
TEST_F(ResponseFrameCommentTest, SimpleExamplePlain) {
  FileFrame header{"response_header_corpus/simple_example_plain"};

  // Validate HEADERS content matches intent.
  auto header_bytes = header.istream();

  // Payload size is 15 bytes.
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x00, 0x1F);
  // HEADERS frame with END_STREAM | END_HEADERS for stream 1.
  EXPECT_NEXT_BYTES(header_bytes, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01);
  // Literal unindexed :status: 200
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x07, 0x3A, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x03, 0x32,
                    0x30, 0x30);
  // Literal unindexed compression: test
  EXPECT_NEXT_BYTES(header_bytes, 0x00, 0x0B, 0x63, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73, 0x73, 0x69,
                    0x6F, 0x6E, 0x04, 0x74, 0x65, 0x73, 0x74);

  // Validate HEADERS decode.
  ClientCodecFrameInjector codec;
  TestClientConnectionImpl connection(
      codec.client_connection_, codec.client_callbacks_, codec.stats_store_, codec.options_,
      Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
      ProdNghttp2SessionFactory::get());
  setupStream(codec, connection);

  EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
  EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
  TestResponseHeaderMapImpl expected_headers;
  expected_headers.addCopy(":status", "200");
  expected_headers.addCopy("compression", "test");
  EXPECT_CALL(codec.response_decoder_, decodeHeaders_(HeaderMapEqual(&expected_headers), true));
  EXPECT_TRUE(codec.write(header.frame(), connection).ok());
}

// Validate that corrupting any single byte with {NUL, CR, LF} in a HEADERS frame doesn't crash or
// trigger ASSERTs. This is a litmus test for the HTTP/2 codec (nghttp2) to demonstrate that it
// doesn't suffer from the issue reported for http-parser in CVE-2019-9900. See also
// https://httpwg.org/specs/rfc7540.html#rfc.section.10.3. We use a non-compressed frame with no
// Huffman encoding to simplify.
TEST_F(RequestFrameCommentTest, SingleByteNulCrLfInHeaderFrame) {
  FileFrame header{"request_header_corpus/simple_example_plain"};

  for (size_t offset = 0; offset < header.frame().size(); ++offset) {
    for (const char c : {'\0', '\n', '\r'}) {
      // Corrupt a single byte in the HEADERS.
      const char original = header.frame()[offset];
      header.frame()[offset] = c;
      // Play the frames back.
      ServerCodecFrameInjector codec;
      TestServerConnectionImpl connection(
          codec.server_connection_, codec.server_callbacks_, codec.stats_store_, codec.options_,
          Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
          envoy::config::core::v3::HttpProtocolOptions::ALLOW);
      EXPECT_TRUE(codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
      EXPECT_CALL(codec.request_decoder_, decodeHeaders_(_, _)).Times(AnyNumber());
      EXPECT_CALL(codec.server_stream_callbacks_, onResetStream(_, _)).Times(AnyNumber());
      auto status = codec.write(header.frame(), connection);
      if (isCodecProtocolError(status)) {
        ENVOY_LOG_MISC(trace, "CodecProtocolError: {}", status.message());
      }
      header.frame()[offset] = original;
    }
  }
}

// Validate that corrupting any single byte with {NUL, CR, LF} in a HEADERS frame doesn't crash or
// trigger ASSERTs. This is a litmus test for the HTTP/2 codec (nghttp2) to demonstrate that it
// doesn't suffer from the issue reported for http-parser in CVE-2019-9900. See also
// https://httpwg.org/specs/rfc7540.html#rfc.section.10.3. We use a non-compressed frame with no
// Huffman encoding to simplify.
TEST_F(ResponseFrameCommentTest, SingleByteNulCrLfInHeaderFrame) {
  FileFrame header{"response_header_corpus/simple_example_plain"};

  for (size_t offset = 0; offset < header.frame().size(); ++offset) {
    for (const char c : {'\0', '\n', '\r'}) {
      // Corrupt a single byte in the HEADERS.
      const char original = header.frame()[offset];
      header.frame()[offset] = c;
      // Play the frames back.
      ClientCodecFrameInjector codec;
      TestClientConnectionImpl connection(
          codec.client_connection_, codec.client_callbacks_, codec.stats_store_, codec.options_,
          Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
          ProdNghttp2SessionFactory::get());
      setupStream(codec, connection);

      EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
      EXPECT_CALL(codec.response_decoder_, decodeHeaders_(_, _)).Times(AnyNumber());
      EXPECT_CALL(codec.client_stream_callbacks_, onResetStream(_, _)).Times(AnyNumber());
      auto status = codec.write(header.frame(), connection);
      if (isCodecProtocolError(status)) {
        ENVOY_LOG_MISC(trace, "CodecProtocolError: {}", status.message());
      }
      header.frame()[offset] = original;
    }
  }
}

// Validate that corrupting any single byte with {NUL, CR, LF} in a HEADERS field name or value
// yields a CodecProtocolException or stream reset. This is a litmus test for the HTTP/2 codec
// (nghttp2) to demonstrate that it doesn't suffer from the issue reported for http-parser in
// CVE-2019-9900. See also https://httpwg.org/specs/rfc7540.html#rfc.section.10.3. We use a
// non-compressed frame with no Huffman encoding to simplify.
TEST_F(RequestFrameCommentTest, SingleByteNulCrLfInHeaderField) {
  FileFrame header{"request_header_corpus/simple_example_plain"};

  for (size_t offset = header.frame().size() - 11 /* foo: offset */; offset < header.frame().size();
       ++offset) {
    for (const char c : {'\0', '\n', '\r'}) {
      // Corrupt a single byte in the HEADERS.
      const char original = header.frame()[offset];
      header.frame()[offset] = c;
      // Play the frames back.
      ServerCodecFrameInjector codec;
      TestServerConnectionImpl connection(
          codec.server_connection_, codec.server_callbacks_, codec.stats_store_, codec.options_,
          Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
          envoy::config::core::v3::HttpProtocolOptions::ALLOW);
      EXPECT_TRUE(codec.write(WellKnownFrames::clientConnectionPrefaceFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
      bool stream_reset = false;
      EXPECT_CALL(codec.request_decoder_, decodeHeaders_(_, _)).Times(0);
      EXPECT_CALL(codec.server_stream_callbacks_, onResetStream(_, _))
          .WillRepeatedly(InvokeWithoutArgs([&stream_reset] { stream_reset = true; }));
      bool codec_exception = false;
      auto status = codec.write(header.frame(), connection);
      if (isCodecProtocolError(status)) {
        codec_exception = true;
      }
      EXPECT_TRUE(stream_reset || codec_exception);
      header.frame()[offset] = original;
    }
  }
}

// Validate that corrupting any single byte with {NUL, CR, LF} in a HEADERS field name or value
// yields a CodecProtocolException or stream reset. This is a litmus test for the HTTP/2 codec
// (nghttp2) to demonstrate that it doesn't suffer from the issue reported for http-parser in
// CVE-2019-9900. See also https://httpwg.org/specs/rfc7540.html#rfc.section.10.3. We use a
// non-compressed frame with no Huffman encoding to simplify.
TEST_F(ResponseFrameCommentTest, SingleByteNulCrLfInHeaderField) {
  FileFrame header{"response_header_corpus/simple_example_plain"};

  for (size_t offset = header.frame().size() - 17 /* test: offset */;
       offset < header.frame().size(); ++offset) {
    for (const char c : {'\0', '\n', '\r'}) {
      // Corrupt a single byte in the HEADERS.
      const char original = header.frame()[offset];
      header.frame()[offset] = c;
      // Play the frames back.
      ClientCodecFrameInjector codec;
      TestClientConnectionImpl connection(
          codec.client_connection_, codec.client_callbacks_, codec.stats_store_, codec.options_,
          Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
          ProdNghttp2SessionFactory::get());
      setupStream(codec, connection);

      EXPECT_TRUE(codec.write(WellKnownFrames::defaultSettingsFrame(), connection).ok());
      EXPECT_TRUE(codec.write(WellKnownFrames::initialWindowUpdateFrame(), connection).ok());
      bool stream_reset = false;
      EXPECT_CALL(codec.response_decoder_, decodeHeaders_(_, _)).Times(0);
      EXPECT_CALL(codec.client_stream_callbacks_, onResetStream(_, _))
          .WillRepeatedly(InvokeWithoutArgs([&stream_reset] { stream_reset = true; }));
      bool codec_exception = false;
      auto status = codec.write(header.frame(), connection);
      if (isCodecProtocolError(status)) {
        codec_exception = true;
      }
      EXPECT_TRUE(stream_reset || codec_exception);
      header.frame()[offset] = original;
    }
  }
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
