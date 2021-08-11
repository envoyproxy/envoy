#include <array>
#include <iostream>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/runtime/runtime_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {
std::string createHeaderFragment(int num_headers) {
  // Create a header field with num_headers headers.
  std::string headers;
  for (int i = 0; i < num_headers; i++) {
    headers += "header" + std::to_string(i) + ": " + "\r\n";
  }
  return headers;
}

std::string createLargeHeaderFragment(int num_headers) {
  // Create a header field with num_headers headers with each header of size ~64 KiB.
  std::string headers;
  for (int i = 0; i < num_headers; i++) {
    headers += "header" + std::to_string(i) + ": " + std::string(64 * 1024, 'q') + "\r\n";
  }
  return headers;
}

Buffer::OwnedImpl createBufferWithNByteSlices(absl::string_view input, size_t max_slice_size) {
  Buffer::OwnedImpl buffer;
  for (size_t offset = 0; offset < input.size(); offset += max_slice_size) {
    buffer.appendSliceForTest(input.substr(offset, max_slice_size));
  }
  // Verify that the buffer contains the right number of slices.
  ASSERT(buffer.getRawSlices().size() == (input.size() + max_slice_size - 1) / max_slice_size);
  return buffer;
}
} // namespace

class Http1CodecTestBase : public testing::Test {
protected:
  Http::Http1::CodecStats& http1CodecStats() {
    return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, store_);
  }

  Stats::TestUtil::TestStore store_;
  Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
};

class Http1ServerConnectionImplTest : public Http1CodecTestBase {
public:
  void initialize() {
    codec_ = std::make_unique<Http1::ServerConnectionImpl>(
        connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
        max_request_headers_count_, headers_with_underscores_action_);
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks_;
  NiceMock<Http1Settings> codec_settings_;
  Http::ServerConnectionPtr codec_;

  void expectHeadersTest(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer,
                         TestRequestHeaderMapImpl& expected_headers);
  void expect400(Protocol p, bool allow_absolute_url, Buffer::OwnedImpl& buffer,
                 absl::string_view details = "");
  void testRequestHeadersExceedLimit(std::string header_string, std::string error_message,
                                     absl::string_view details = "");
  void testTrailersExceedLimit(std::string trailer_string, std::string error_message,
                               bool enable_trailers);
  void testRequestHeadersAccepted(std::string header_string);
  // Used to test if trailers are decoded/encoded
  void expectTrailersTest(bool enable_trailers);

  void testServerAllowChunkedContentLength(uint32_t content_length, bool allow_chunked_length);

  // Send the request, and validate the received request headers.
  // Then send a response just to clean up.
  void
  sendAndValidateRequestAndSendResponse(absl::string_view raw_request,
                                        const TestRequestHeaderMapImpl& expected_request_headers) {
    Buffer::OwnedImpl buffer(raw_request);
    sendAndValidateRequestAndSendResponse(buffer, expected_request_headers);
  }

  void
  sendAndValidateRequestAndSendResponse(Buffer::Instance& buffer,
                                        const TestRequestHeaderMapImpl& expected_request_headers) {
    NiceMock<MockRequestDecoder> decoder;
    Http::ResponseEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));
    EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_request_headers), true));
    auto status = codec_->dispatch(buffer);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(0U, buffer.length());
    response_encoder->encodeHeaders(TestResponseHeaderMapImpl{{":status", "200"}}, true);
  }

protected:
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
      headers_with_underscores_action_{envoy::config::core::v3::HttpProtocolOptions::ALLOW};
};

void Http1ServerConnectionImplTest::expect400(Protocol p, bool allow_absolute_url,
                                              Buffer::OwnedImpl& buffer,
                                              absl::string_view details) {
  InSequence sequence;

  if (allow_absolute_url) {
    codec_settings_.allow_absolute_url_ = allow_absolute_url;
    codec_ = std::make_unique<Http1::ServerConnectionImpl>(
        connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
        max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  }

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  EXPECT_CALL(decoder, sendLocalReply(Http::Code::BadRequest, "Bad Request", _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(p, codec_->protocol());
  if (!details.empty()) {
    EXPECT_EQ(details, response_encoder->getStream().responseDetails());
  }
}

void Http1ServerConnectionImplTest::expectHeadersTest(Protocol p, bool allow_absolute_url,
                                                      Buffer::OwnedImpl& buffer,
                                                      TestRequestHeaderMapImpl& expected_headers) {
  InSequence sequence;

  // Make a new 'codec' with the right settings
  if (allow_absolute_url) {
    codec_settings_.allow_absolute_url_ = allow_absolute_url;
    codec_ = std::make_unique<Http1::ServerConnectionImpl>(
        connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
        max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  }

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(p, codec_->protocol());
}

void Http1ServerConnectionImplTest::expectTrailersTest(bool enable_trailers) {
  initialize();

  // Make a new 'codec' with the right settings
  if (enable_trailers) {
    codec_settings_.enable_trailers_ = enable_trailers;
    codec_ = std::make_unique<Http1::ServerConnectionImpl>(
        connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
        max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  }

  InSequence sequence;
  StrictMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder&, bool) -> RequestDecoder& { return decoder; }));

  EXPECT_CALL(decoder, decodeHeaders_(_, false));

  Buffer::OwnedImpl expected_data("Hello World");
  if (enable_trailers) {
    // Verify that body data is delivered before trailers.
    EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
    EXPECT_CALL(decoder, decodeTrailers_);
  } else {
    EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
    EXPECT_CALL(decoder, decodeData(_, true));
  }

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n"
                           "6\r\nHello \r\n"
                           "5\r\nWorld\r\n"
                           "0\r\nhello: world\r\nsecond: header\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

void Http1ServerConnectionImplTest::testTrailersExceedLimit(std::string trailer_string,
                                                            std::string error_message,
                                                            bool enable_trailers) {
  initialize();
  // Make a new 'codec' with the right settings
  codec_settings_.enable_trailers_ = enable_trailers;
  codec_ = std::make_unique<Http1::ServerConnectionImpl>(
      connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
      max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);
  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder&, bool) -> RequestDecoder& { return decoder; }));

  if (enable_trailers) {
    EXPECT_CALL(decoder, decodeHeaders_(_, false));
    EXPECT_CALL(decoder, decodeData(_, false));
  } else {
    EXPECT_CALL(decoder, decodeData(_, false));
    EXPECT_CALL(decoder, decodeData(_, true));
  }

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\n"
                           "Host: host\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "4\r\n"
                           "body\r\n0\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  buffer = Buffer::OwnedImpl(trailer_string);
  if (enable_trailers) {
    EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
    status = codec_->dispatch(buffer);
    EXPECT_TRUE(isCodecProtocolError(status));
    EXPECT_EQ(status.message(), error_message);
  } else {
    // If trailers are not enabled, we expect Envoy to simply skip over the large
    // trailers as if nothing has happened!
    status = codec_->dispatch(buffer);
    EXPECT_TRUE(status.ok());
  }
}
void Http1ServerConnectionImplTest::testRequestHeadersExceedLimit(std::string header_string,
                                                                  std::string error_message,
                                                                  absl::string_view details) {
  initialize();

  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  buffer = Buffer::OwnedImpl(header_string + "\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), error_message);
  if (!details.empty()) {
    EXPECT_EQ(details, response_encoder->getStream().responseDetails());
  }
}

void Http1ServerConnectionImplTest::testRequestHeadersAccepted(std::string header_string) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  auto status = codec_->dispatch(buffer);
  buffer = Buffer::OwnedImpl(header_string + "\r\n");
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

void Http1ServerConnectionImplTest::testServerAllowChunkedContentLength(uint32_t content_length,
                                                                        bool allow_chunked_length) {
  codec_settings_.allow_chunked_length_ = allow_chunked_length;
  codec_ = std::make_unique<Http1::ServerConnectionImpl>(
      connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
      max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;

  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "chunked"},
  };
  Buffer::OwnedImpl expected_data("Hello World");

  if (allow_chunked_length) {
    EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
    EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
    EXPECT_CALL(decoder, decodeData(_, true));
  } else {
    EXPECT_CALL(decoder, decodeHeaders_(_, _)).Times(0);
    EXPECT_CALL(decoder, decodeData(_, _)).Times(0);
    EXPECT_CALL(decoder, sendLocalReply(Http::Code::BadRequest, "Bad Request", _, _, _));
  }

  Buffer::OwnedImpl buffer(
      fmt::format("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\ncontent-length: {}\r\n\r\n"
                  "6\r\nHello \r\n"
                  "5\r\nWorld\r\n"
                  "0\r\n\r\n",
                  content_length));

  auto status = codec_->dispatch(buffer);

  if (allow_chunked_length) {
    EXPECT_TRUE(status.ok());
  } else {
    EXPECT_TRUE(isCodecProtocolError(status));
    EXPECT_EQ(status.message(),
              "http/1.1 protocol error: both 'Content-Length' and 'Transfer-Encoding' are set.");
    EXPECT_EQ("http1.content_length_and_chunked_not_allowed",
              response_encoder->getStream().responseDetails());
  }
}

TEST_F(Http1ServerConnectionImplTest, EmptyHeader) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {"Test", ""},
      {"Hello", "World"},
      {":path", "/"},
      {":method", "GET"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nTest:\r\nHello: World\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// We support the identity encoding, but because it does not end in chunked encoding we reject it
// per RFC 7230 Section 3.3.3
TEST_F(Http1ServerConnectionImplTest, IdentityEncodingNoChunked) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\ntransfer-encoding: identity\r\n\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: unsupported transfer encoding");
}

TEST_F(Http1ServerConnectionImplTest, UnsupportedEncoding) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\ntransfer-encoding: gzip\r\n\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: unsupported transfer encoding");
}

// Verify that the optimization which moves large slices of body instead of copying them is working.
// Note that this test is validating a performance optimization, not a functional behavior
// requirement. If future changes to the codec make this test not pass, but do not regress
// performance of large HTTP body handling, this test can be changed or removed.
TEST_F(Http1ServerConnectionImplTest, LargeBodyOptimization) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  std::string post = "POST / HTTP/1.1\r\ncontent-length: 1000000\r\n\r\n";
  post += std::string(50000, '0');
  Buffer::OwnedImpl buffer = createBufferWithNByteSlices(post, 16384);

  // Remember the original buffer slices, but not the first one because it contains
  // non-body and will not have the optimization applied to it.
  auto original_slices = buffer.getRawSlices();
  original_slices.erase(original_slices.begin());

  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(_, false)).WillOnce(Invoke([&](Buffer::Instance& body, bool) {
    auto end_slices = body.getRawSlices();
    end_slices.erase(end_slices.begin());
    EXPECT_EQ(original_slices, end_slices);
  }));

  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Regression test for checking if content length exists when all bits are set (e.g. 3).
TEST_F(Http1ServerConnectionImplTest, ContentLengthAllBitsSet) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {"content-length", "3"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false))
      .WillOnce(Invoke([&](Http::RequestHeaderMapPtr&, bool) -> void {
        connection_.state_ = Network::Connection::State::Closing;
      }));
  EXPECT_CALL(decoder, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 3\r\n\r\n123");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_NE(0U, buffer.length());
}

// Verify that data in the two body chunks is merged before the call to decodeData.
TEST_F(Http1ServerConnectionImplTest, ChunkedBody) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "chunked"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  Buffer::OwnedImpl expected_data("Hello World");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  // Call to decodeData("", true) happens after.
  Buffer::OwnedImpl empty("");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&empty), true));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n"
                           "6\r\nHello \r\n"
                           "5\r\nWorld\r\n"
                           "0\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Verify dispatch behavior when dispatching an incomplete chunk, and resumption of the parse via a
// second dispatch.
TEST_F(Http1ServerConnectionImplTest, ChunkedBodySplitOverTwoDispatches) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "chunked"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  Buffer::OwnedImpl expected_data1("Hello Worl");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n"
                           "6\r\nHello \r\n"
                           "5\r\nWorl");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  // Process the rest of the body and final chunk.
  Buffer::OwnedImpl expected_data2("d");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), false));
  EXPECT_CALL(decoder, decodeData(_, true));

  Buffer::OwnedImpl buffer2("d\r\n"
                            "0\r\n\r\n");
  status = codec_->dispatch(buffer2);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer2.length());
}

// Verify that headers and chunked body are processed correctly and data is merged before the
// decodeData call even if delivered in a buffer that holds 1 byte per slice.
TEST_F(Http1ServerConnectionImplTest, ChunkedBodyFragmentedBuffer) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "chunked"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  Buffer::OwnedImpl expected_data("Hello World");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  EXPECT_CALL(decoder, decodeData(_, true));

  Buffer::OwnedImpl buffer =
      createBufferWithNByteSlices("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n"
                                  "6\r\nHello \r\n"
                                  "5\r\nWorld\r\n"
                                  "0\r\n\r\n",
                                  1);
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, ChunkedBodyCase) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "Chunked"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  Buffer::OwnedImpl expected_data("Hello World");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  EXPECT_CALL(decoder, decodeData(_, true));

  Buffer::OwnedImpl buffer(
      "POST / HTTP/1.1\r\ntransfer-encoding: Chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Verify that body dispatch does not happen after detecting a parse error processing a chunk
// header.
TEST_F(Http1ServerConnectionImplTest, InvalidChunkHeader) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":path", "/"},
      {":method", "POST"},
      {"transfer-encoding", "chunked"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  EXPECT_CALL(decoder, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n"
                           "6\r\nHello \r\n"
                           "invalid\r\nWorl");

  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: HPE_INVALID_CHUNK_SIZE");
}

TEST_F(Http1ServerConnectionImplTest, IdentityAndChunkedBody) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ntransfer-encoding: "
                           "identity,chunked\r\n\r\nb\r\nHello World\r\n0\r\n\r\n");

  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: unsupported transfer encoding");
}

TEST_F(Http1ServerConnectionImplTest, HostWithLWS) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "host"}, {":path", "/"}, {":method", "GET"}};

  // Regression test spaces before and after the host header value.
  sendAndValidateRequestAndSendResponse("GET / HTTP/1.1\r\nHost: host \r\n\r\n", expected_headers);

  // Regression test tabs before and after the host header value.
  sendAndValidateRequestAndSendResponse("GET / HTTP/1.1\r\nHost:	host	\r\n\r\n",
                                        expected_headers);

  // Regression test mixed spaces and tabs before and after the host header value.
  sendAndValidateRequestAndSendResponse(
      "GET / HTTP/1.1\r\nHost: 	 	  host		  	 \r\n\r\n", expected_headers);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10270. Linear whitespace at the
// beginning and end of a header value should be stripped. Whitespace in the middle should be
// preserved.
TEST_F(Http1ServerConnectionImplTest, InnerLWSIsPreserved) {
  initialize();

  // Header with many spaces surrounded by non-whitespace characters to ensure that dispatching is
  // split across multiple dispatch calls. The threshold used here comes from Envoy preferring 16KB
  // reads, but the important part is that the header value is split such that the pieces have
  // leading and trailing whitespace characters.
  const std::string header_value_with_inner_lws = "v" + std::string(32 * 1024, ' ') + "v";
  TestRequestHeaderMapImpl expected_headers{{":authority", "host"},
                                            {":path", "/"},
                                            {":method", "GET"},
                                            {"header_field", header_value_with_inner_lws}};

  {
    // Regression test spaces in the middle are preserved
    Buffer::OwnedImpl header_buffer = createBufferWithNByteSlices(
        "GET / HTTP/1.1\r\nHost: host\r\nheader_field: " + header_value_with_inner_lws + "\r\n\r\n",
        16 * 1024);
    EXPECT_EQ(3, header_buffer.getRawSlices().size());
    sendAndValidateRequestAndSendResponse(header_buffer, expected_headers);
  }

  {
    // Regression test spaces before and after are removed
    Buffer::OwnedImpl header_buffer = createBufferWithNByteSlices(
        "GET / HTTP/1.1\r\nHost: host\r\nheader_field:  " + header_value_with_inner_lws +
            "  \r\n\r\n",
        16 * 1024);
    EXPECT_EQ(3, header_buffer.getRawSlices().size());
    sendAndValidateRequestAndSendResponse(header_buffer, expected_headers);
  }
}

TEST_F(Http1ServerConnectionImplTest, CodecHasCorrectStreamErrorIfTrue) {
  codec_settings_.stream_error_on_invalid_http_message_ = true;
  codec_ = std::make_unique<Http1::ServerConnectionImpl>(
      connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
      max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(response_encoder->streamErrorOnInvalidHttpMessage());
}

TEST_F(Http1ServerConnectionImplTest, CodecHasCorrectStreamErrorIfFalse) {
  codec_settings_.stream_error_on_invalid_http_message_ = false;
  codec_ = std::make_unique<Http1::ServerConnectionImpl>(
      connection_, http1CodecStats(), callbacks_, codec_settings_, max_request_headers_kb_,
      max_request_headers_count_, envoy::config::core::v3::HttpProtocolOptions::ALLOW);

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  auto status = codec_->dispatch(buffer);
  EXPECT_FALSE(response_encoder->streamErrorOnInvalidHttpMessage());
}

TEST_F(Http1ServerConnectionImplTest, CodecHasDefaultStreamErrorIfNotSet) {
  initialize();

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  auto status = codec_->dispatch(buffer);
  EXPECT_FALSE(response_encoder->streamErrorOnInvalidHttpMessage());
}

TEST_F(Http1ServerConnectionImplTest, Http10) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(Protocol::Http10, codec_->protocol());
}

TEST_F(Http1ServerConnectionImplTest, Http10AbsoluteNoOp) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET / HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http10Absolute) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                            {":scheme", "http"},
                                            {":path", "/foobar"},
                                            {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foobar HTTP/1.0\r\n\r\n");
  expectHeadersTest(Protocol::Http10, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http10MultipleResponses) {
  initialize();

  MockRequestDecoder decoder;
  // Send a full HTTP/1.0 request and proxy a response.
  {
    Buffer::OwnedImpl buffer(
        "GET /foobar HTTP/1.0\r\nHost: www.somewhere.com\r\nconnection: keep-alive\r\n\r\n");
    Http::ResponseEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));

    EXPECT_CALL(decoder, decodeHeaders_(_, true));
    auto status = codec_->dispatch(buffer);
    EXPECT_TRUE(status.ok());

    std::string output;
    ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));
    TestResponseHeaderMapImpl headers{{":status", "200"}};
    response_encoder->encodeHeaders(headers, true);
    EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
    EXPECT_EQ(Protocol::Http10, codec_->protocol());
  }

  // Now send an HTTP/1.1 request and make sure the protocol is tracked correctly.
  {
    TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                              {":scheme", "http"},
                                              {":path", "/foobar"},
                                              {":method", "GET"}};
    Buffer::OwnedImpl buffer("GET /foobar HTTP/1.1\r\nHost: www.somewhere.com\r\n\r\n");

    Http::ResponseEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));
    EXPECT_CALL(decoder, decodeHeaders_(_, true));
    auto status = codec_->dispatch(buffer);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(Protocol::Http11, codec_->protocol());
  }
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath1) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":scheme", "http"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePath2) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                            {":scheme", "http"},
                                            {":path", "/foo/bar"},
                                            {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathWithPort) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com:4532"},
                                            {":scheme", "http"},
                                            {":path", "/foo/bar"},
                                            {":method", "GET"}};
  Buffer::OwnedImpl buffer(
      "GET http://www.somewhere.com:4532/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathWithHttps) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                            {":scheme", "https"},
                                            {":path", "/foo/bar"},
                                            {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET https://www.somewhere.com/foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsoluteEnabledNoOp) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "/foo/bar"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET /foo/bar HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11InvalidRequest) {
  initialize();

  // Invalid because www.somewhere.com is not an absolute path nor an absolute url
  Buffer::OwnedImpl buffer("GET www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer, "http1.codec_error");
}

TEST_F(Http1ServerConnectionImplTest, Http11InvalidTrailerPost) {
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder&, bool) -> RequestDecoder& { return decoder; }));

  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  // Verify that body is delivered as soon as the final chunk marker is found, even if an error is
  // found while processing trailers.
  Buffer::OwnedImpl expected_data("body");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\n"
                           "Host: host\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n"
                           "4\r\n"
                           "body\r\n0\r\n"
                           "badtrailer\r\n\r\n");

  EXPECT_CALL(decoder, sendLocalReply(Http::Code::BadRequest, "Bad Request", _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathNoSlash) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":scheme", "http"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePathBad) {
  initialize();

  Buffer::OwnedImpl buffer("GET * HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer, "http1.invalid_url");
}

TEST_F(Http1ServerConnectionImplTest, Http11AbsolutePortTooLarge) {
  initialize();

  Buffer::OwnedImpl buffer("GET http://foobar.com:1000000 HTTP/1.1\r\nHost: bah\r\n\r\n");
  expect400(Protocol::Http11, true, buffer);
}

TEST_F(Http1ServerConnectionImplTest, SketchyConnectionHeader) {
  initialize();

  Buffer::OwnedImpl buffer(
      "GET / HTTP/1.1\r\nHost: bah\r\nConnection: a,b,c,d,e,f,g,h,i,j,k,l,m\r\n\r\n");
  expect400(Protocol::Http11, true, buffer, "http1.connection_header_rejected");
}

TEST_F(Http1ServerConnectionImplTest, Http11RelativeOnly) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "bah"}, {":path", "http://www.somewhere.com/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, false, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, Http11Options) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":path", "*"}, {":method", "OPTIONS"}};
  Buffer::OwnedImpl buffer("OPTIONS * HTTP/1.1\r\nHost: www.somewhere.com\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, SimpleGet) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{{":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Test that if the stream is not created at the time an error is detected, it
// is created as part of sending the protocol error.
TEST_F(Http1ServerConnectionImplTest, BadRequestNoStream) {
  initialize();

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  // Check that before any headers are parsed, requests do not look like HEAD or gRPC requests.
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));

  Buffer::OwnedImpl buffer("bad");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
}

// This behavior was observed during CVE-2019-18801 and helped to limit the
// scope of affected Envoy configurations.
TEST_F(Http1ServerConnectionImplTest, RejectInvalidMethod) {
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("BAD / HTTP/1.1\r\nHost: foo\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
}

TEST_F(Http1ServerConnectionImplTest, BadRequestStartedStream) {
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("G");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());

  Buffer::OwnedImpl buffer2("g");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
}

TEST_F(Http1ServerConnectionImplTest, FloodProtection) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Buffer::OwnedImpl local_buffer;
  // Read a request and send a response, without draining the response from the
  // connection buffer. The first two should not cause problems.
  for (int i = 0; i < 2; ++i) {
    Http::ResponseEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](Http::ResponseEncoder& encoder, bool) -> Http::RequestDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));

    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
    auto status = codec_->dispatch(buffer);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(0U, buffer.length());

    // In most tests the write output is serialized to a buffer here it is
    // ignored to build up queued "end connection" sentinels.
    EXPECT_CALL(connection_, write(_, _))

        .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> void {
          // Move the response out of data while preserving the buffer fragment sentinels.
          local_buffer.move(data);
        }));

    TestResponseHeaderMapImpl headers{{":status", "200"}};
    response_encoder->encodeHeaders(headers, true);
  }

  // Trying to accept a third request with two buffered responses in the queue should trigger flood
  // protection.
  {
    Http::ResponseEncoder* response_encoder = nullptr;
    EXPECT_CALL(callbacks_, newStream(_, _))
        .WillOnce(Invoke([&](Http::ResponseEncoder& encoder, bool) -> Http::RequestDecoder& {
          response_encoder = &encoder;
          return decoder;
        }));

    Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
    auto status = codec_->dispatch(buffer);
    EXPECT_TRUE(isBufferFloodError(status));
    EXPECT_EQ(status.message(), "Too many responses queued.");
    EXPECT_EQ(1, store_.counter("http1.response_flood").value());
  }
}

TEST_F(Http1ServerConnectionImplTest, HostHeaderTranslation) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "hello"}, {":path", "/"}, {":method", "GET"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nHOST: hello\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Ensures that requests with invalid HTTP header values are properly rejected
// when the runtime guard is enabled for the feature.
TEST_F(Http1ServerConnectionImplTest, HeaderInvalidCharsRejection) {
  TestScopedRuntime scoped_runtime;
  // When the runtime-guarded feature is enabled, invalid header values
  // should result in a rejection.

  initialize();

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer(
      absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: ", std::string(1, 3), "\r\n"));
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: header value contains invalid chars");
  EXPECT_EQ("http1.invalid_characters", response_encoder->getStream().responseDetails());
}

// Ensures that request headers with names containing the underscore character are allowed
// when the option is set to allow.
TEST_F(Http1ServerConnectionImplTest, HeaderNameWithUnderscoreAllowed) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "h.com"},
      {":path", "/"},
      {":method", "GET"},
      {"foo_bar", "bar"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer(absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo_bar: bar\r\n\r\n"));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(0, store_.counter("http1.dropped_headers_with_underscores").value());
}

// Ensures that request headers with names containing the underscore character are dropped
// when the option is set to drop headers.
TEST_F(Http1ServerConnectionImplTest, HeaderNameWithUnderscoreAreDropped) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER;
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "h.com"},
      {":path", "/"},
      {":method", "GET"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), true));

  Buffer::OwnedImpl buffer(absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo_bar: bar\r\n\r\n"));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
  EXPECT_EQ(1, store_.counter("http1.dropped_headers_with_underscores").value());
}

// Ensures that request with header names containing the underscore character are rejected
// when the option is set to reject request.
TEST_F(Http1ServerConnectionImplTest, HeaderNameWithUnderscoreCauseRequestRejected) {
  headers_with_underscores_action_ = envoy::config::core::v3::HttpProtocolOptions::REJECT_REQUEST;
  initialize();

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer(absl::StrCat("GET / HTTP/1.1\r\nHOST: h.com\r\nfoo_bar: bar\r\n\r\n"));
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: header name contains underscores");
  EXPECT_EQ("http1.unexpected_underscore", response_encoder->getStream().responseDetails());
  EXPECT_EQ(1, store_.counter("http1.requests_rejected_with_underscores_in_headers").value());
}

TEST_F(Http1ServerConnectionImplTest, HeaderInvalidAuthority) {
  TestScopedRuntime scoped_runtime;

  initialize();

  MockRequestDecoder decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer(absl::StrCat("GET / HTTP/1.1\r\nHOST: h.\"com\r\n\r\n"));
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(),
            "http/1.1 protocol error: request headers failed spec compliance checks");
  EXPECT_EQ("http.invalid_authority", response_encoder->getStream().responseDetails());
}

// Mutate an HTTP GET with embedded NULs, this should always be rejected in some
// way (not necessarily with "head value contains NUL" though).
TEST_F(Http1ServerConnectionImplTest, HeaderMutateEmbeddedNul) {
  const std::string example_input = "GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: barbaz\r\n";

  for (size_t n = 1; n < example_input.size(); ++n) {
    initialize();

    InSequence sequence;

    MockRequestDecoder decoder;
    EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

    Buffer::OwnedImpl buffer(
        absl::StrCat(example_input.substr(0, n), std::string(1, '\0'), example_input.substr(n)));
    EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
    auto status = codec_->dispatch(buffer);
    EXPECT_FALSE(status.ok());
    EXPECT_TRUE(isCodecProtocolError(status));
    EXPECT_THAT(status.message(), testing::HasSubstr("http/1.1 protocol error:"));
  }
}

// Mutate an HTTP GET with CR or LF. These can cause an error status or maybe
// result in a valid decodeHeaders(). In any case, the validHeaderString()
// ASSERTs should validate we never have any embedded CR or LF.
TEST_F(Http1ServerConnectionImplTest, HeaderMutateEmbeddedCRLF) {
  const std::string example_input = "GET / HTTP/1.1\r\nHOST: h.com\r\nfoo: barbaz\r\n";

  for (const char c : {'\r', '\n'}) {
    for (size_t n = 1; n < example_input.size(); ++n) {
      initialize();

      InSequence sequence;

      NiceMock<MockRequestDecoder> decoder;
      EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

      Buffer::OwnedImpl buffer(
          absl::StrCat(example_input.substr(0, n), std::string(1, c), example_input.substr(n)));
      // May or may not cause an error status, but should never trip on a debug ASSERT.
      auto status = codec_->dispatch(buffer);
    }
  }
}

TEST_F(Http1ServerConnectionImplTest, CloseDuringHeadersComplete) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {"content-length", "5"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false))
      .WillOnce(Invoke([&](Http::RequestHeaderMapPtr&, bool) -> void {
        connection_.state_ = Network::Connection::State::Closing;
      }));
  EXPECT_CALL(decoder, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_NE(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, PostWithContentLength) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {"content-length", "5"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));

  Buffer::OwnedImpl expected_data1("12345");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false));

  Buffer::OwnedImpl expected_data2;
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), true));

  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

// Verify that headers and body with content length are processed correctly and data is merged
// before the decodeData call even if delivered in a buffer that holds 1 byte per slice.
TEST_F(Http1ServerConnectionImplTest, PostWithContentLengthFragmentedBuffer) {
  initialize();

  InSequence sequence;

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {"content-length", "5"}, {":path", "/"}, {":method", "POST"}};
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));

  Buffer::OwnedImpl expected_data1("12345");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false));

  Buffer::OwnedImpl expected_data2;
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), true));

  Buffer::OwnedImpl buffer =
      createBufferWithNByteSlices("POST / HTTP/1.1\r\ncontent-length: 5\r\n\r\n12345", 1);
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponse) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
}

// As with Http1ClientConnectionImplTest.LargeHeaderRequestEncode but validate
// the response encoder instead of request encoder.
TEST_F(Http1ServerConnectionImplTest, LargeHeaderResponseEncode) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  const std::string long_header_value = std::string(79 * 1024, 'a');
  TestResponseHeaderMapImpl headers{{":status", "200"}, {"foo", long_header_value}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\nfoo: " + long_header_value + "\r\ncontent-length: 0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseTrainProperHeaders) {
  codec_settings_.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"some-header", "foo"}, {"some#header", "baz"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\nSome-Header: foo\r\nSome#Header: baz\r\nContent-Length: 0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, 304ResponseTransferEncodingNotAddedWhenContentLengthPresent) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nif-none-match: \"1234567890\"\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{
      {":status", "304"}, {"etag", "\"1234567890\""}, {"content-length", "123"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 304 Not Modified\r\netag: \"1234567890\"\r\ncontent-length: 123\r\n\r\n",
            output);
}

// Upstream response 304 without content-length header
// 304 Response does not need to have Transfer-Encoding added even it's allowed by RFC 7230,
// Section 3.3.1. Both GET and HEAD response are the same and consistent
TEST_F(Http1ServerConnectionImplTest,
       304ResponseTransferEncodingContentLengthNotAddedWhenContentLengthNotPresent) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nif-none-match: \"1234567890\"\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "304"}, {"etag", "\"1234567890\""}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 304 Not Modified\r\netag: \"1234567890\"\r\n\r\n", output);

  buffer.add("HEAD / HTTP/1.1\r\nif-none-match: \"1234567890\"\r\n\r\n");
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  output.clear();
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 304 Not Modified\r\netag: \"1234567890\"\r\n\r\n", output);
}

// Upstream response 304 without content-length header
// The legacy behavior returns different headers for GET and HEAD requests
// For GET, it adds "content-length: 0"
// For HEAD, it adds "transfer-encoding: chunked"
TEST_F(Http1ServerConnectionImplTest,
       304ResponseTransferEncodingContentLengthNotAddedWhenContentLengthNotPresentLegacy) {
  // Testing old behavior with no_chunked_encoding_header_for_304 turned off
  // GET and HEAD returns different headers
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.no_chunked_encoding_header_for_304", "false"}});
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nif-none-match: \"1234567890\"\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "304"}, {"etag", "\"1234567890\""}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 304 Not Modified\r\netag: \"1234567890\"\r\ncontent-length: 0\r\n\r\n",
            output);

  buffer.add("HEAD / HTTP/1.1\r\nif-none-match: \"1234567890\"\r\n\r\n");
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  output.clear();
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ(
      "HTTP/1.1 304 Not Modified\r\netag: \"1234567890\"\r\ntransfer-encoding: chunked\r\n\r\n",
      output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseWith204) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "204"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 204 No Content\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HeaderOnlyResponseWith100Then200) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  response_encoder->encode100ContinueHeaders(continue_headers);
  EXPECT_EQ("HTTP/1.1 100 Continue\r\n\r\n", output);
  output.clear();

  // Test the special case where we encode 100 headers (no content length may be
  // appended) then 200 headers (content length 0 will be appended).
  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, MetadataTest) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
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

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(Invoke([&output](Buffer::Instance& data, bool) {
    // Verify that individual writes into the codec's output buffer were coalesced into a single
    // slice
    ASSERT_EQ(1, data.getRawSlices().size());
    output.append(data.toString());
    data.drain(data.length());
  }));

  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);

  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
            "World\r\n0\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, ChunkedResponseWithTrailers) {
  codec_settings_.enable_trailers_ = true;
  initialize();
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, false);

  TestResponseTrailerMapImpl trailers{{"foo", "bar"}, {"foo", "baz"}};
  response_encoder->encodeTrailers(trailers);

  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
            "World\r\n0\r\nfoo: bar\r\nfoo: baz\r\n\r\n",
            output);
}

TEST_F(Http1ServerConnectionImplTest, ContentLengthResponse) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-length", "11"}};
  response_encoder->encodeHeaders(headers, false);

  Buffer::OwnedImpl data("Hello World");
  response_encoder->encodeData(data, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 11\r\n\r\nHello World", output);
}

TEST_F(Http1ServerConnectionImplTest, HeadRequestResponse) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("HEAD / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-length", "5"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, HeadChunkedRequestResponse) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("HEAD / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, true);
  EXPECT_EQ("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, DoubleRequest) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  std::string request("GET / HTTP/1.1\r\n\r\n");
  Buffer::OwnedImpl buffer(request);
  buffer.add(request);

  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(request.size(), buffer.length());

  response_encoder->encodeHeaders(TestResponseHeaderMapImpl{{":status", "200"}}, true);

  status = codec_->dispatch(buffer);
  EXPECT_EQ(0U, buffer.length());
}

TEST_F(Http1ServerConnectionImplTest, RequestWithTrailersDropped) { expectTrailersTest(false); }

TEST_F(Http1ServerConnectionImplTest, RequestWithTrailersKept) { expectTrailersTest(true); }

TEST_F(Http1ServerConnectionImplTest, IgnoreUpgradeH2c) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "www.somewhere.com"}, {":scheme", "http"}, {":path", "/"}, {":method", "GET"}};
  Buffer::OwnedImpl buffer(
      "GET http://www.somewhere.com/ HTTP/1.1\r\nConnection: "
      "Upgrade, HTTP2-Settings\r\nUpgrade: h2c\r\nHTTP2-Settings: token64\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, IgnoreUpgradeH2cClose) {
  initialize();

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                            {":scheme", "http"},
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

  TestRequestHeaderMapImpl expected_headers{{":authority", "www.somewhere.com"},
                                            {":scheme", "http"},
                                            {":path", "/"},
                                            {":method", "GET"},
                                            {"connection", "Close"}};
  Buffer::OwnedImpl buffer("GET http://www.somewhere.com/ HTTP/1.1\r\nConnection: "
                           "Upgrade, Close, HTTP2-Settings, Etc\r\nUpgrade: h2c\r\nHTTP2-Settings: "
                           "token64\r\nHost: bah\r\n\r\n");
  expectHeadersTest(Protocol::Http11, true, buffer, expected_headers);
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequest) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl buffer(
      "POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: foo\r\ncontent-length:5\r\n\r\n");
  auto status = codec_->dispatch(buffer);

  Buffer::OwnedImpl expected_data1("12345");
  Buffer::OwnedImpl body("12345");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data1), false));
  status = codec_->dispatch(body);

  Buffer::OwnedImpl expected_data2("abcd");
  Buffer::OwnedImpl websocket_payload("abcd");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data2), false));
  status = codec_->dispatch(websocket_payload);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithEarlyData) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: "
                           "foo\r\ncontent-length:5\r\n\r\n12345abcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithTEChunked) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Even with T-E chunked, the data should neither be inspected for (the not
  // present in this unit test) chunks, but simply passed through.
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl buffer("POST / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: "
                           "foo\r\ntransfer-encoding: chunked\r\n\r\n12345abcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, UpgradeRequestWithNoBody) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Make sure we avoid the deferred_end_stream_headers_ optimization for
  // requests-with-no-body.
  Buffer::OwnedImpl expected_data("abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl buffer(
      "GET / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: foo\r\ncontent-length: 0\r\n\r\nabcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

// Test that 101 upgrade responses do not contain content-length or transfer-encoding headers.
TEST_F(Http1ServerConnectionImplTest, UpgradeRequestResponseHeaders) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: foo\r\n\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(0U, buffer.length());

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestResponseHeaderMapImpl headers{{":status", "101"}};
  response_encoder->encodeHeaders(headers, false);
  EXPECT_EQ("HTTP/1.1 101 Switching Protocols\r\n\r\n", output);
}

TEST_F(Http1ServerConnectionImplTest, ConnectRequestNoContentLength) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  TestRequestHeaderMapImpl expected_headers{
      {":authority", "host:80"},
      {":method", "CONNECT"},
  };
  EXPECT_CALL(decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
  Buffer::OwnedImpl buffer("CONNECT host:80 HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);

  Buffer::OwnedImpl expected_data("abcd");
  Buffer::OwnedImpl connect_payload("abcd");
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  status = codec_->dispatch(connect_payload);
  EXPECT_TRUE(status.ok());
}

// We use the absolute URL parsing code for CONNECT requests, but it does not
// actually allow absolute URLs.
TEST_F(Http1ServerConnectionImplTest, ConnectRequestAbsoluteURLNotallowed) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl buffer("CONNECT http://host:80 HTTP/1.1\r\n\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
}

TEST_F(Http1ServerConnectionImplTest, ConnectRequestWithEarlyData) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  Buffer::OwnedImpl expected_data("abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl buffer("CONNECT host:80 HTTP/1.1\r\n\r\nabcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, ConnectRequestWithTEChunked) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Per https://tools.ietf.org/html/rfc7231#section-4.3.6 CONNECT with body has no defined
  // semantics: Envoy will reject chunked CONNECT requests.
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  Buffer::OwnedImpl buffer(
      "CONNECT host:80 HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n12345abcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: unsupported transfer encoding");
}

TEST_F(Http1ServerConnectionImplTest, ConnectRequestWithNonZeroContentLength) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Make sure we avoid the deferred_end_stream_headers_ optimization for
  // requests-with-no-body.
  Buffer::OwnedImpl buffer("CONNECT host:80 HTTP/1.1\r\ncontent-length: 1\r\n\r\nabcd");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "http/1.1 protocol error: unsupported content length");
}

TEST_F(Http1ServerConnectionImplTest, ConnectRequestWithZeroContentLength) {
  initialize();

  InSequence sequence;
  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _)).WillOnce(ReturnRef(decoder));

  // Make sure we avoid the deferred_end_stream_headers_ optimization for
  // requests-with-no-body.
  Buffer::OwnedImpl expected_data("abcd");
  EXPECT_CALL(decoder, decodeHeaders_(_, false));
  EXPECT_CALL(decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl buffer("CONNECT host:80 HTTP/1.1\r\ncontent-length: 0\r\n\r\nabcd");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, WatermarkTest) {
  EXPECT_CALL(connection_, bufferLimit()).WillOnce(Return(10));
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n\r\n");
  auto status = codec_->dispatch(buffer);

  Http::MockStreamCallbacks stream_callbacks;
  response_encoder->getStream().addCallbacks(stream_callbacks);

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ServerConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  TestResponseHeaderMapImpl headers{{":status", "200"}};
  response_encoder->encodeHeaders(headers, false);

  // Fake out the underlying Network::Connection buffer being drained.
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark());
  static_cast<ServerConnection*>(codec_.get())
      ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

TEST_F(Http1ServerConnectionImplTest, TestSmugglingDisallowChunkedContentLength0) {
  testServerAllowChunkedContentLength(0, false);
}
TEST_F(Http1ServerConnectionImplTest, TestSmugglingDisallowChunkedContentLength1) {
  // content-length less than POST body size
  testServerAllowChunkedContentLength(1, false);
}
TEST_F(Http1ServerConnectionImplTest, TestSmugglingDisallowChunkedContentLength100) {
  // content-length greater than POST body size
  testServerAllowChunkedContentLength(100, false);
}

TEST_F(Http1ServerConnectionImplTest, TestSmugglingAllowChunkedContentLength0) {
  testServerAllowChunkedContentLength(0, true);
}
TEST_F(Http1ServerConnectionImplTest, TestSmugglingAllowChunkedContentLength1) {
  // content-length less than POST body size
  testServerAllowChunkedContentLength(1, true);
}
TEST_F(Http1ServerConnectionImplTest, TestSmugglingAllowChunkedContentLength100) {
  // content-length greater than POST body size
  testServerAllowChunkedContentLength(100, true);
}

TEST_F(Http1ServerConnectionImplTest,
       ShouldDumpParsedAndPartialHeadersWithoutAllocatingMemoryIfProcessingHeaders) {
  initialize();

  MockRequestDecoder decoder;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder&, bool) -> RequestDecoder& { return decoder; }));

  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  Buffer::OwnedImpl headers("POST / HTTP/1.1\r\n"
                            "Host: host\r\n"
                            "Accept-Language: en\r\n"
                            "Connection: keep-alive\r\n"
                            "Unfinished-Header: Not-Finished-Value");

  auto status = codec_->dispatch(headers);
  EXPECT_TRUE(status.ok());

  // Dumps the header map without allocating memory
  Stats::TestUtil::MemoryTest memory_test;
  dynamic_cast<Http1::ServerConnectionImpl*>(codec_.get())->dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check dump contents for completed headers and partial headers.
  EXPECT_THAT(
      ostream.contents(),
      testing::HasSubstr("absl::get<RequestHeaderMapPtr>(headers_or_trailers_): \n  ':authority', "
                         "'host'\n  'accept-language', 'en'\n  'connection', 'keep-alive'"));
  EXPECT_THAT(ostream.contents(),
              testing::HasSubstr("header_parsing_state_: Value, current_header_field_: "
                                 "Unfinished-Header, current_header_value_: Not-Finished-Value"));
}

TEST_F(Http1ServerConnectionImplTest, ShouldDumpDispatchBufferWithoutAllocatingMemory) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder&, bool) -> RequestDecoder& { return decoder; }));

  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  // Dump the body
  // Set content length to enable us to dumpState before
  // buffers are drained. Only the first slice should be dumped.
  Buffer::OwnedImpl request;
  request.appendSliceForTest("POST / HTTP/1.1\r\n"
                             "Content-Length: 5\r\n"
                             "\r\n"
                             "Hello");
  request.appendSliceForTest("GarbageDataShouldNotBeDumped");
  EXPECT_CALL(decoder, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) {
        // dumpState here before buffers are drained. No memory should be allocated.
        Stats::TestUtil::MemoryTest memory_test;
        dynamic_cast<Http1::ServerConnectionImpl*>(codec_.get())->dumpState(ostream, 0);
        EXPECT_EQ(memory_test.consumedBytes(), 0);
      }))
      .WillOnce(Invoke([]() {}));

  auto status = codec_->dispatch(request);
  EXPECT_TRUE(status.ok());

  // Check dump contents
  EXPECT_THAT(ostream.contents(), HasSubstr("buffered_body_.length(): 5, header_parsing_state_: "
                                            "Done, current_header_field_: , current_header_value_: "
                                            "\nactive_request_: \n, request_url_: null"
                                            ", response_encoder_.local_end_stream_: 0"));
  EXPECT_THAT(ostream.contents(),
              HasSubstr("current_dispatching_buffer_ front_slice length: 43 contents: \"POST / "
                        "HTTP/1.1\\r\\nContent-Length: 5\\r\\n\\r\\nHello\"\n"));
}

class Http1ClientConnectionImplTest : public Http1CodecTestBase {
public:
  void initialize() {
    codec_ = std::make_unique<Http1::ClientConnectionImpl>(
        connection_, http1CodecStats(), callbacks_, codec_settings_, max_response_headers_count_);
  }

  void readDisableOnRequestEncoder(RequestEncoder* request_encoder, bool disable) {
    dynamic_cast<Http1::RequestEncoderImpl*>(request_encoder)->readDisable(disable);
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockConnectionCallbacks> callbacks_;
  NiceMock<Http1Settings> codec_settings_;
  Http::ClientConnectionPtr codec_;

  void testClientAllowChunkedContentLength(uint32_t content_length, bool allow_chunked_length);

protected:
  Stats::TestUtil::TestStore store_;
  uint32_t max_response_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
};

void Http1ClientConnectionImplTest::testClientAllowChunkedContentLength(uint32_t content_length,
                                                                        bool allow_chunked_length) {
  codec_settings_.allow_chunked_length_ = allow_chunked_length;
  codec_ = std::make_unique<Http1::ClientConnectionImpl>(
      connection_, http1CodecStats(), callbacks_, codec_settings_, max_response_headers_count_);

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}, {"transfer-encoding", "chunked"}};
  Buffer::OwnedImpl expected_data("Hello World");

  if (allow_chunked_length) {
    EXPECT_CALL(response_decoder, decodeHeaders_(HeaderMapEqual(&expected_headers), false));
    EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data), false));
    EXPECT_CALL(response_decoder, decodeData(_, true));
  } else {
    EXPECT_CALL(response_decoder, decodeHeaders_(_, _)).Times(0);
    EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  }

  Buffer::OwnedImpl buffer(
      fmt::format("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\ncontent-length: {}\r\n\r\n"
                  "6\r\nHello \r\n"
                  "5\r\nWorld\r\n"
                  "0\r\n\r\n",
                  content_length));
  auto status = codec_->dispatch(buffer);

  if (allow_chunked_length) {
    EXPECT_TRUE(status.ok());
  } else {
    EXPECT_TRUE(isCodecProtocolError(status));
    EXPECT_EQ(status.message(),
              "http/1.1 protocol error: both 'Content-Length' and 'Transfer-Encoding' are set.");
  };
}

TEST_F(Http1ClientConnectionImplTest, SimpleGet) {
  initialize();

  MockResponseDecoder response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, SimpleGetWithHeaderCasing) {
  codec_settings_.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;

  initialize();

  MockResponseDecoder response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {"my-custom-header", "hey"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\nMy-Custom-Header: hey\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, SimpleGetWithHeaderCasingLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.dont_add_content_length_for_bodiless_requests", "false"}});
  codec_settings_.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;

  initialize();

  MockResponseDecoder response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {"my-custom-header", "hey"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\nMy-Custom-Header: hey\r\nContent-Length: 0\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, HostHeaderTranslate) {
  initialize();

  MockResponseDecoder response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\n\r\n", output);
}

TEST_F(Http1ClientConnectionImplTest, Reset) {
  initialize();

  MockResponseDecoder response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);

  Http::MockStreamCallbacks callbacks;
  request_encoder.getStream().addCallbacks(callbacks);
  EXPECT_CALL(callbacks, onResetStream(StreamResetReason::LocalReset, _));
  request_encoder.getStream().resetStream(StreamResetReason::LocalReset);
}

// Verify that we correctly enable reads on the connection when the final response is
// received.
TEST_F(Http1ClientConnectionImplTest, FlowControlReadDisabledReenable) {
  initialize();

  MockResponseDecoder response_decoder;
  auto* request_encoder = &codec_->newStream(response_decoder);
  // Manually read disable.
  EXPECT_CALL(connection_, readDisable(true)).Times(2);
  readDisableOnRequestEncoder(request_encoder, true);
  readDisableOnRequestEncoder(request_encoder, true);

  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));

  // Request.
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder->encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\n\r\n", output);
  output.clear();

  // When the response is sent, the read disable should be unwound.
  EXPECT_CALL(connection_, readDisable(false)).Times(2);

  // Response.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, PrematureResponse) {
  initialize();

  Buffer::OwnedImpl response("HTTP/1.1 408 Request Timeout\r\nConnection: Close\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(isPrematureResponseError(status));
}

TEST_F(Http1ClientConnectionImplTest, EmptyBodyResponse503) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, EmptyBodyResponse200) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, HeadRequest) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "HEAD"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, 204Response) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

// 204 No Content with Content-Length is barred by RFC 7230, Section 3.3.2.
TEST_F(Http1ClientConnectionImplTest, 204ResponseContentLengthNotAllowed) {
  // By default, content-length is barred.
  {
    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 20\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_FALSE(status.ok());
  }

  // Test with feature disabled: content-length allowed.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.require_strict_1xx_and_204_response_headers", "false"}});

    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 20\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_TRUE(status.ok());
  }
}

// 204 No Content with Content-Length: 0 is technically barred by RFC 7230, Section 3.3.2, but we
// allow it.
TEST_F(Http1ClientConnectionImplTest, 204ResponseWithContentLength0) {
  {
    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 0\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_TRUE(status.ok());
  }

  // Test with feature disabled: content-length allowed.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.require_strict_1xx_and_204_response_headers", "false"}});

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nContent-Length: 0\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_TRUE(status.ok());
  }
}

// 204 No Content with Transfer-Encoding headers is barred by RFC 7230, Section 3.3.1.
TEST_F(Http1ClientConnectionImplTest, 204ResponseTransferEncodingNotAllowed) {
  // By default, transfer-encoding is barred.
  {
    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_FALSE(status.ok());
  }

  // Test with feature disabled: transfer-encoding allowed.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.require_strict_1xx_and_204_response_headers", "false"}});

    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response("HTTP/1.1 204 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_TRUE(status.ok());
  }
}

// 100 response followed by 200 results in a [decode100ContinueHeaders, decodeHeaders] sequence.
TEST_F(Http1ClientConnectionImplTest, ContinueHeaders) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decode100ContinueHeaders_(_));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl initial_response("HTTP/1.1 100 Continue\r\n\r\n");
  auto status = codec_->dispatch(initial_response);
  EXPECT_TRUE(status.ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\n");
  status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

// Multiple 100 responses are passed to the response encoder (who is responsible for coalescing).
TEST_F(Http1ClientConnectionImplTest, MultipleContinueHeaders) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decode100ContinueHeaders_(_));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl initial_response("HTTP/1.1 100 Continue\r\n\r\n");
  auto status = codec_->dispatch(initial_response);
  EXPECT_TRUE(status.ok());

  EXPECT_CALL(response_decoder, decode100ContinueHeaders_(_));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl another_100_response("HTTP/1.1 100 Continue\r\n\r\n");
  status = codec_->dispatch(another_100_response);
  EXPECT_TRUE(status.ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  EXPECT_CALL(response_decoder, decodeData(_, _)).Times(0);
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\n");
  status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

// 101/102 headers etc. are passed to the response encoder (who is responsibly for deciding to
// upgrade, ignore, etc.).
TEST_F(Http1ClientConnectionImplTest, 1xxNonContinueHeaders) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response("HTTP/1.1 102 Processing\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

// 101 Switching Protocol with Transfer-Encoding headers is barred by RFC 7230, Section 3.3.1.
TEST_F(Http1ClientConnectionImplTest, 101ResponseTransferEncodingNotAllowed) {
  // By default, transfer-encoding is barred.
  {
    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response(
        "HTTP/1.1 101 Switching Protocols\r\nTransfer-Encoding: chunked\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_FALSE(status.ok());
  }

  // Test with feature disabled: transfer-encoding allowed.
  {
    TestScopedRuntime scoped_runtime;
    Runtime::LoaderSingleton::getExisting()->mergeValues(
        {{"envoy.reloadable_features.require_strict_1xx_and_204_response_headers", "false"}});

    initialize();

    NiceMock<MockResponseDecoder> response_decoder;
    Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
    TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
    EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

    Buffer::OwnedImpl response(
        "HTTP/1.1 101 Switching Protocols\r\nTransfer-Encoding: chunked\r\n\r\n");
    auto status = codec_->dispatch(response);
    EXPECT_TRUE(status.ok());
  }
}

TEST_F(Http1ClientConnectionImplTest, BadEncodeParams) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;

  // Invalid outbound data errors are impossible to trigger in normal processing, since bad
  // downstream data would have been rejected by the codec, and erroneous filter processing would
  // cause a direct response by the filter manager. An invalid status is returned from new codecs
  // which protects against future extensions or header modifications after the filter chain. The
  // old codecs will still throw an exception (that presently will be uncaught in contexts like
  // sendLocalReply).
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  EXPECT_THAT(
      request_encoder.encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}}, true).message(),
      testing::HasSubstr("missing required"));
  EXPECT_THAT(
      request_encoder.encodeHeaders(TestRequestHeaderMapImpl{{":method", "GET"}}, true).message(),
      testing::HasSubstr("missing required"));
}

TEST_F(Http1ClientConnectionImplTest, NoContentLengthResponse) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl expected_data1("Hello World");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data1), false));

  Buffer::OwnedImpl expected_data2;
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data2), true));

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\nHello World");
  auto status = codec_->dispatch(response);

  Buffer::OwnedImpl empty;
  status = codec_->dispatch(empty);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, ResponseWithTrailers) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello "
                             "World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_EQ(0UL, response.length());
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, GiantPath) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/" + std::string(16384, 'a')}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, PrematureUpgradeResponse) {
  initialize();

  // make sure upgradeAllowed doesn't cause crashes if run with no pending response.
  Buffer::OwnedImpl response(
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: upgrade\r\nUpgrade: websocket\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(isPrematureResponseError(status));
}

TEST_F(Http1ClientConnectionImplTest, UpgradeResponse) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"},
                                   {":path", "/"},
                                   {":authority", "host"},
                                   {"connection", "upgrade"},
                                   {"upgrade", "websocket"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send upgrade headers
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response(
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: upgrade\r\nUpgrade: websocket\r\n\r\n");
  auto status = codec_->dispatch(response);

  // Send body payload
  Buffer::OwnedImpl expected_data1("12345");
  Buffer::OwnedImpl body("12345");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data1), false));
  status = codec_->dispatch(body);

  // Send websocket payload
  Buffer::OwnedImpl expected_data2("abcd");
  Buffer::OwnedImpl websocket_payload("abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data2), false));
  status = codec_->dispatch(websocket_payload);
  EXPECT_TRUE(status.ok());
}

// Same data as above, but make sure directDispatch immediately hands off any
// outstanding data.
TEST_F(Http1ClientConnectionImplTest, UpgradeResponseWithEarlyData) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"},
                                   {":path", "/"},
                                   {":authority", "host"},
                                   {"connection", "upgrade"},
                                   {"upgrade", "websocket"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send upgrade headers
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: "
                             "upgrade\r\nUpgrade: websocket\r\n\r\n12345abcd");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, ConnectResponse) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send response headers
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n");
  auto status = codec_->dispatch(response);

  // Send body payload
  Buffer::OwnedImpl expected_data1("12345");
  Buffer::OwnedImpl body("12345");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data1), false));
  status = codec_->dispatch(body);

  // Send connect payload
  Buffer::OwnedImpl expected_data2("abcd");
  Buffer::OwnedImpl connect_payload("abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data2), false));
  status = codec_->dispatch(connect_payload);
  EXPECT_TRUE(status.ok());
}

// Same data as above, but make sure directDispatch immediately hands off any
// outstanding data.
TEST_F(Http1ClientConnectionImplTest, ConnectResponseWithEarlyData) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send response headers and payload
  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\n\r\n12345abcd");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, ConnectRejected) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  EXPECT_CALL(response_decoder, decodeHeaders_(_, false));
  Buffer::OwnedImpl expected_data("12345abcd");
  EXPECT_CALL(response_decoder, decodeData(BufferEqual(&expected_data), false));
  Buffer::OwnedImpl response("HTTP/1.1 400 OK\r\n\r\n12345abcd");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ClientConnectionImplTest, WatermarkTest) {
  EXPECT_CALL(connection_, bufferLimit()).WillOnce(Return(10));
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
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
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

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

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  Http::MockStreamCallbacks stream_callbacks;
  request_encoder.getStream().addCallbacks(stream_callbacks);

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  auto status = codec_->dispatch(response);

  Buffer::OwnedImpl response2("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
  status = codec_->dispatch(response2);
  EXPECT_TRUE(isPrematureResponseError(status));

  // Fake a call for going below the low watermark. Make sure no stream callbacks get called.
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10655. Make sure we correctly
// handle going below low watermark when closing the connection during a completion callback.
TEST_F(Http1ClientConnectionImplTest, LowWatermarkDuringClose) {
  initialize();

  InSequence s;

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  Http::MockStreamCallbacks stream_callbacks;
  request_encoder.getStream().addCallbacks(stream_callbacks);

  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Fake a call from the underlying Network::Connection and verify the stream is notified.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  static_cast<ClientConnection*>(codec_.get())
      ->onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(response_decoder, decodeHeaders_(_, true))
      .WillOnce(Invoke([&](ResponseHeaderMapPtr&, bool) {
        // Fake a call for going below the low watermark. Make sure no stream callbacks get called.
        EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark()).Times(0);
        static_cast<ClientConnection*>(codec_.get())
            ->onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_TRUE(status.ok());
}

TEST_F(Http1ServerConnectionImplTest, LargeTrailersRejected) {
  // Default limit of 60 KiB
  std::string long_string = "big: " + std::string(60 * 1024, 'q') + "\r\n\r\n\r\n";
  testTrailersExceedLimit(long_string, "trailers size exceeds limit", true);
}

TEST_F(Http1ServerConnectionImplTest, LargeTrailerFieldRejected) {
  // Construct partial headers with a long field name that exceeds the default limit of 60KiB.
  std::string long_string = "bigfield" + std::string(60 * 1024, 'q');
  testTrailersExceedLimit(long_string, "trailers size exceeds limit", true);
}

// Tests that the default limit for the number of request headers is 100.
TEST_F(Http1ServerConnectionImplTest, ManyTrailersRejected) {
  // Send a request with 101 headers.
  testTrailersExceedLimit(createHeaderFragment(101) + "\r\n\r\n", "trailers count exceeds limit",
                          true);
}

TEST_F(Http1ServerConnectionImplTest, LargeTrailersRejectedIgnored) {
  // Default limit of 60 KiB
  std::string long_string = "big: " + std::string(60 * 1024, 'q') + "\r\n\r\n\r\n";
  testTrailersExceedLimit(long_string, "trailers size exceeds limit", false);
}

TEST_F(Http1ServerConnectionImplTest, LargeTrailerFieldRejectedIgnored) {
  // Default limit of 60 KiB
  std::string long_string = "bigfield" + std::string(60 * 1024, 'q') + ": value\r\n\r\n\r\n";
  testTrailersExceedLimit(long_string, "trailers size exceeds limit", false);
}

// Tests that the default limit for the number of request headers is 100.
TEST_F(Http1ServerConnectionImplTest, ManyTrailersIgnored) {
  // Send a request with 101 headers.
  testTrailersExceedLimit(createHeaderFragment(101) + "\r\n\r\n", "trailers count exceeds limit",
                          false);
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestUrlRejected) {
  initialize();

  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));

  // Default limit of 60 KiB
  std::string long_url = "/" + std::string(60 * 1024, 'q');
  Buffer::OwnedImpl buffer("GET " + long_url + " HTTP/1.1\r\n");

  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers size exceeds limit");
  EXPECT_EQ("http1.headers_too_large", response_encoder->getStream().responseDetails());
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersRejected) {
  // Default limit of 60 KiB
  std::string long_string = "big: " + std::string(60 * 1024, 'q') + "\r\n";
  testRequestHeadersExceedLimit(long_string, "headers size exceeds limit", "");
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersRejectedBeyondMaxConfigurable) {
  max_request_headers_kb_ = 8192;
  std::string long_string = "big: " + std::string(8193 * 1024, 'q') + "\r\n";
  testRequestHeadersExceedLimit(long_string, "headers size exceeds limit", "");
}

// Tests that the default limit for the number of request headers is 100.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersRejected) {
  // Send a request with 101 headers.
  testRequestHeadersExceedLimit(createHeaderFragment(101), "headers count exceeds limit",
                                "http1.too_many_headers");
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersSplitRejected) {
  // Default limit of 60 KiB
  initialize();

  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  auto status = codec_->dispatch(buffer);

  std::string long_string = std::string(1024, 'q');
  for (int i = 0; i < 59; i++) {
    buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
    status = codec_->dispatch(buffer);
  }
  // the 60th 1kb header should induce overflow
  buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers size exceeds limit");
  EXPECT_EQ("http1.headers_too_large", response_encoder->getStream().responseDetails());
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersSplitRejectedMaxConfigurable) {
  max_request_headers_kb_ = 8192;
  max_request_headers_count_ = 150;
  initialize();

  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  auto status = codec_->dispatch(buffer);

  std::string long_string = std::string(64 * 1024, 'q');
  for (int i = 0; i < 127; i++) {
    buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
    status = codec_->dispatch(buffer);
  }
  // the 128th 64kb header should induce overflow
  buffer = Buffer::OwnedImpl(fmt::format("big: {}\r\n", long_string));
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers size exceeds limit");
  EXPECT_EQ("http1.headers_too_large", response_encoder->getStream().responseDetails());
}

// Tests that the 101th request header causes overflow with the default max number of request
// headers.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersSplitRejected) {
  // Default limit of 100.
  initialize();

  std::string exception_reason;
  NiceMock<MockRequestDecoder> decoder;
  Http::ResponseEncoder* response_encoder = nullptr;
  EXPECT_CALL(callbacks_, newStream(_, _))
      .WillOnce(Invoke([&](ResponseEncoder& encoder, bool) -> RequestDecoder& {
        response_encoder = &encoder;
        return decoder;
      }));
  Buffer::OwnedImpl buffer("GET / HTTP/1.1\r\n");
  auto status = codec_->dispatch(buffer);

  // Dispatch 100 headers.
  buffer = Buffer::OwnedImpl(createHeaderFragment(100));
  status = codec_->dispatch(buffer);

  // The final 101th header should induce overflow.
  buffer = Buffer::OwnedImpl("header101:\r\n\r\n");
  EXPECT_CALL(decoder, sendLocalReply(_, _, _, _, _));
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers count exceeds limit");
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersAccepted) {
  max_request_headers_kb_ = 4096;
  std::string long_string = "big: " + std::string(1024 * 1024, 'q') + "\r\n";
  testRequestHeadersAccepted(long_string);
}

TEST_F(Http1ServerConnectionImplTest, LargeRequestHeadersAcceptedMaxConfigurable) {
  max_request_headers_kb_ = 8192;
  std::string long_string = "big: " + std::string(8191 * 1024, 'q') + "\r\n";
  testRequestHeadersAccepted(long_string);
}

// Tests that the number of request headers is configurable.
TEST_F(Http1ServerConnectionImplTest, ManyRequestHeadersAccepted) {
  max_request_headers_count_ = 150;
  // Create a request with 150 headers.
  testRequestHeadersAccepted(createHeaderFragment(150));
}

TEST_F(Http1ServerConnectionImplTest, ManyLargeRequestHeadersAccepted) {
  max_request_headers_kb_ = 8192;
  // Create a request with 64 headers, each header of size ~64 KiB. Total size ~4MB.
  testRequestHeadersAccepted(createLargeHeaderFragment(64));
}

// Tests that incomplete response headers of 80 kB header value fails.
TEST_F(Http1ClientConnectionImplTest, ResponseHeadersWithLargeValueRejected) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  std::string long_header = "big: " + std::string(80 * 1024, 'q');
  buffer = Buffer::OwnedImpl(long_header);
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers size exceeds limit");
}

// Tests that incomplete response headers with a 80 kB header field fails.
TEST_F(Http1ClientConnectionImplTest, ResponseHeadersWithLargeFieldRejected) {
  initialize();

  NiceMock<MockRequestDecoder> decoder;
  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  std::string long_header = "big: " + std::string(80 * 1024, 'q');
  buffer = Buffer::OwnedImpl(long_header);
  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers size exceeds limit");
}

// Tests that the size of response headers for HTTP/1 must be under 80 kB.
TEST_F(Http1ClientConnectionImplTest, LargeResponseHeadersAccepted) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  auto status = codec_->dispatch(buffer);
  EXPECT_TRUE(status.ok());
  std::string long_header = "big: " + std::string(79 * 1024, 'q') + "\r\n";
  buffer = Buffer::OwnedImpl(long_header);
  status = codec_->dispatch(buffer);
}

// Regression test for CVE-2019-18801. Large method headers should not trigger
// ASSERTs or ASAN, which they previously did.
TEST_F(Http1ClientConnectionImplTest, LargeMethodRequestEncode) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  const std::string long_method = std::string(79 * 1024, 'a');
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{
      {":method", long_method}, {":path", "/"}, {":authority", "host"}};
  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ(long_method + " / HTTP/1.1\r\nhost: host\r\ncontent-length: 0\r\n\r\n", output);
}

// As with LargeMethodEncode, but for the path header. This was not an issue
// in CVE-2019-18801, but the related code does explicit size calculations on
// both path and method (these are the two distinguished headers). So,
// belt-and-braces.
TEST_F(Http1ClientConnectionImplTest, LargePathRequestEncode) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  const std::string long_path = std::string(79 * 1024, '/');
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", long_path}, {":authority", "host"}};
  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET " + long_path + " HTTP/1.1\r\nhost: host\r\n\r\n", output);
}

// As with LargeMethodEncode, but for an arbitrary header. This was not an issue
// in CVE-2019-18801.
TEST_F(Http1ClientConnectionImplTest, LargeHeaderRequestEncode) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  const std::string long_header_value = std::string(79 * 1024, 'a');
  TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {"foo", long_header_value}, {":path", "/"}, {":authority", "host"}};
  std::string output;
  ON_CALL(connection_, write(_, _)).WillByDefault(AddBufferToString(&output));
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());
  EXPECT_EQ("GET / HTTP/1.1\r\nhost: host\r\nfoo: " + long_header_value + "\r\n\r\n", output);
}

// Exception called when the number of response headers exceeds the default value of 100.
TEST_F(Http1ClientConnectionImplTest, ManyResponseHeadersRejected) {
  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  auto status = codec_->dispatch(buffer);
  buffer = Buffer::OwnedImpl(createHeaderFragment(101) + "\r\n");

  status = codec_->dispatch(buffer);
  EXPECT_TRUE(isCodecProtocolError(status));
  EXPECT_EQ(status.message(), "headers count exceeds limit");
}

// Tests that the number of response headers is configurable.
TEST_F(Http1ClientConnectionImplTest, ManyResponseHeadersAccepted) {
  max_response_headers_count_ = 152;

  initialize();

  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n");
  auto status = codec_->dispatch(buffer);
  // Response already contains one header.
  buffer = Buffer::OwnedImpl(createHeaderFragment(150) + "\r\n");
  status = codec_->dispatch(buffer);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplit0) {
  testClientAllowChunkedContentLength(0, false);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplit1) {
  testClientAllowChunkedContentLength(1, false);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplit100) {
  testClientAllowChunkedContentLength(100, false);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplitAllowChunkedLength0) {
  testClientAllowChunkedContentLength(0, true);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplitAllowChunkedLength1) {
  testClientAllowChunkedContentLength(1, true);
}

TEST_F(Http1ClientConnectionImplTest, TestResponseSplitAllowChunkedLength100) {
  testClientAllowChunkedContentLength(100, true);
}

TEST_F(Http1ClientConnectionImplTest,
       ShouldDumpParsedAndPartialHeadersWithoutAllocatingMemoryIfProcessingHeaders) {
  initialize();

  // Send request and dispatch response without headers completed.
  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nserver: foo\r\nContent-Length: 8");
  auto status = codec_->dispatch(response);
  EXPECT_EQ(0UL, response.length());
  EXPECT_TRUE(status.ok());

  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  dynamic_cast<Http1::ClientConnectionImpl*>(codec_.get())->dumpState(ostream, 0);

  // Check for header map and partial headers.
  EXPECT_THAT(ostream.contents(),
              testing::HasSubstr(
                  "absl::get<ResponseHeaderMapPtr>(headers_or_trailers_): \n  'server', 'foo'\n"));
  EXPECT_THAT(ostream.contents(),
              testing::HasSubstr("header_parsing_state_: Value, current_header_field_: "
                                 "Content-Length, current_header_value_: 8"));
}

TEST_F(Http1ClientConnectionImplTest, ShouldDumpDispatchBufferWithoutAllocatingMemory) {
  initialize();

  // Send request
  NiceMock<MockResponseDecoder> response_decoder;
  Http::RequestEncoder& request_encoder = codec_->newStream(response_decoder);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send response; dumpState while parsing response.
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  EXPECT_CALL(response_decoder, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) {
        // dumpState here before buffers are drained. No memory should be allocated.
        Stats::TestUtil::MemoryTest memory_test;
        dynamic_cast<Http1::ClientConnectionImpl*>(codec_.get())->dumpState(ostream, 0);
        EXPECT_EQ(memory_test.consumedBytes(), 0);
      }))
      .WillOnce(Invoke([]() {}));

  // Dump the body
  // Set content length to enable us to dumpState before
  // buffers are drained. Only the first slice should be dumped.
  Buffer::OwnedImpl response;
  response.appendSliceForTest("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello");
  response.appendSliceForTest("GarbageDataShouldNotBeDumped");
  auto status = codec_->dispatch(response);
  // Client codec complains about extraneous data.
  EXPECT_EQ(response.length(), 28UL);
  EXPECT_FALSE(status.ok());

  // Check for body data.
  EXPECT_THAT(ostream.contents(), HasSubstr("buffered_body_.length(): 5, header_parsing_state_: "
                                            "Done"));
  EXPECT_THAT(ostream.contents(),
              testing::HasSubstr("current_dispatching_buffer_ front_slice length: 43 contents: "
                                 "\"HTTP/1.1 200 OK\\r\\nContent-Length: 5\\r\\n\\r\\nHello\"\n"));
}

TEST_F(Http1ClientConnectionImplTest, ShouldDumpCorrespondingRequestWithoutAllocatingMemory) {
  initialize();

  // Send request
  Router::MockUpstreamToDownstream upstream_to_downstream;
  Http::RequestEncoder& request_encoder = codec_->newStream(upstream_to_downstream);
  TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(request_encoder.encodeHeaders(headers, true).ok());

  // Send response; dumpState while parsing response.
  std::array<char, 2048> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  // Expect dumpState call
  EXPECT_CALL(upstream_to_downstream, dumpState(_, _));

  EXPECT_CALL(upstream_to_downstream, decodeHeaders(_, _)).WillOnce(InvokeWithoutArgs([&]() {
    // dumpState here before buffers are drained. No memory should be allocated.
    Stats::TestUtil::MemoryTest memory_test;
    dynamic_cast<Http1::ClientConnectionImpl*>(codec_.get())->dumpState(ostream, 1);
    EXPECT_EQ(memory_test.consumedBytes(), 0);
  }));

  Buffer::OwnedImpl response("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n");
  auto status = codec_->dispatch(response);
  EXPECT_EQ(response.length(), 0UL);
  EXPECT_TRUE(status.ok());

  // Check contents for corresponding downstream request
  EXPECT_THAT(ostream.contents(), testing::HasSubstr("Dumping corresponding downstream request:"));
}

} // namespace Http
} // namespace Envoy
