#include <cstdint>
#include <memory>

#include "source/extensions/filters/network/generic_proxy/codecs/http1/config.h"

#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Http1 {
namespace {

using testing::NiceMock;

TEST(Http1MessageFrameTest, Http1MessageFrameTest) {
  // Protocol.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    HttpRequestFrame frame(std::move(headers), true);

    EXPECT_EQ(frame.protocol(), "http1");
  }

  // ForEach.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    // Add some headers.
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    // Add some custom headers.
    headers->addCopy(Http::LowerCaseString("custom"), "value");

    HttpRequestFrame frame(std::move(headers), true);

    // Check that the headers are iterated correctly.
    size_t count = 0;
    frame.forEach([&count](absl::string_view, absl::string_view) -> bool {
      count++;
      return true;
    });

    EXPECT_EQ(count, 4);
  }

  // ForEach but return break;
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    // Add some headers.
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    // Add some custom headers.
    headers->addCopy(Http::LowerCaseString("custom"), "value");

    HttpRequestFrame frame(std::move(headers), true);

    // Check that the headers are iterated correctly.
    size_t count = 0;
    frame.forEach([&count](absl::string_view, absl::string_view) -> bool {
      count++;
      return false;
    });

    EXPECT_EQ(count, 1);
  }

  // Get.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    // Add some headers.
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    // Add some custom headers.
    headers->addCopy(Http::LowerCaseString("custom"), "value");

    HttpRequestFrame frame(std::move(headers), true);

    // Check that the headers are retrieved correctly.
    EXPECT_EQ(frame.get("host").value(), "host");
    EXPECT_EQ(frame.get(":authority").value(), "host");
    EXPECT_EQ(frame.get(":path").value(), "/path");
    EXPECT_EQ(frame.get(":method").value(), "GET");
    EXPECT_EQ(frame.get("custom").value(), "value");
  }

  // Set.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    HttpRequestFrame frame(std::move(headers), true);

    // Set some headers.
    frame.set("host", "host");
    frame.set(":path", "/path");
    frame.set(":method", "POST");
    frame.set("custom", "value");

    // Check that the headers are set correctly.
    EXPECT_EQ(frame.get("host").value(), "host");
    EXPECT_EQ(frame.get(":authority").value(), "host");
    EXPECT_EQ(frame.get(":path").value(), "/path");
    EXPECT_EQ(frame.get(":method").value(), "POST");
    EXPECT_EQ(frame.get("custom").value(), "value");
  }

  // Erase.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    // Add some headers.
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    // Add some custom headers.
    headers->addCopy(Http::LowerCaseString("custom"), "value");

    HttpRequestFrame frame(std::move(headers), true);

    // Erase some headers.
    frame.erase("host");
    frame.erase(":path");
    frame.erase(":method");
    frame.erase("custom");

    // Check that the headers are erased correctly.
    EXPECT_EQ(frame.get("host"), absl::nullopt);
    EXPECT_EQ(frame.get(":authority"), absl::nullopt);
    EXPECT_EQ(frame.get(":path"), absl::nullopt);
    EXPECT_EQ(frame.get(":method"), absl::nullopt);
    EXPECT_EQ(frame.get("custom"), absl::nullopt);
  }

  // Request FrameFlags.
  {
    auto headers = Http::RequestHeaderMapImpl::create();
    HttpRequestFrame frame(std::move(headers), true);

    EXPECT_EQ(true, frame.frameFlags().endStream());

    auto headers2 = Http::RequestHeaderMapImpl::create();
    HttpRequestFrame frame2(std::move(headers2), false);

    EXPECT_EQ(false, frame2.frameFlags().endStream());
  }

  // Response FrameFlags.
  {
    auto headers = Http::ResponseHeaderMapImpl::create();
    HttpResponseFrame frame(std::move(headers), true);

    EXPECT_EQ(true, frame.frameFlags().endStream());
    EXPECT_EQ(false, frame.frameFlags().drainClose());

    auto headers2 = Http::ResponseHeaderMapImpl::create();
    headers2->setConnection("close");
    HttpResponseFrame frame2(std::move(headers2), false);

    EXPECT_EQ(false, frame2.frameFlags().endStream());
    EXPECT_EQ(true, frame2.frameFlags().drainClose());
  }

  // Request FrameType.
  {
    auto headers = Http::RequestHeaderMapImpl::create();

    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");

    HttpRequestFrame frame(std::move(headers), true);

    EXPECT_EQ(frame.host(), "host");
    EXPECT_EQ(frame.path(), "/path");
    EXPECT_EQ(frame.method(), "GET");
  }

  // Response FrameType.
  {
    // 200
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    HttpResponseFrame frame(std::move(headers), true);

    EXPECT_EQ(frame.status().code(), 200);
    EXPECT_TRUE(frame.status().ok());

    // 404
    auto headers2 = Http::ResponseHeaderMapImpl::create();
    headers2->setStatus(404);
    HttpResponseFrame frame2(std::move(headers2), true);

    EXPECT_EQ(frame2.status().code(), 404);
    EXPECT_TRUE(frame2.status().ok());

    // 500
    auto headers3 = Http::ResponseHeaderMapImpl::create();
    headers3->setStatus(500);
    HttpResponseFrame frame3(std::move(headers3), true);

    EXPECT_EQ(frame3.status().code(), 500);
    EXPECT_FALSE(frame3.status().ok());

    // 503
    auto headers4 = Http::ResponseHeaderMapImpl::create();
    headers4->setStatus(503);
    HttpResponseFrame frame4(std::move(headers4), true);

    EXPECT_EQ(frame4.status().code(), 503);
    EXPECT_FALSE(frame4.status().ok());

    // Invalid status code value.
    auto headers5 = Http::ResponseHeaderMapImpl::create();
    headers5->addCopy(Http::Headers::get().Status, "xxx");
    HttpResponseFrame frame5(std::move(headers5), true);

    EXPECT_EQ(frame5.status().code(), -1);
    EXPECT_FALSE(frame5.status().ok());
  }

  // Raw body frame.
  {
    Buffer::OwnedImpl buffer("body");

    HttpRawBodyFrame frame(buffer, true);

    EXPECT_EQ(frame.frameFlags().endStream(), true);

    EXPECT_EQ(frame.buffer().length(), 4);
    EXPECT_EQ(frame.buffer().toString(), "body");

    Buffer::OwnedImpl new_buffer;
    new_buffer.move(frame.buffer());

    EXPECT_EQ(new_buffer.toString(), "body");

    EXPECT_EQ(frame.buffer().length(), 0);
  }
}

class Http1ServerCodecTest : public testing::Test {
public:
  Http1ServerCodecTest() { initializeCodec(); }

  void initializeCodec(bool single_frame_mode = false, uint32_t max_buffer_size = 8 * 1024 * 1024) {
    codec_ = std::make_unique<Http1ServerCodec>(single_frame_mode, max_buffer_size);
    codec_->setCodecCallbacks(codec_callbacks_);
  }

  NiceMock<MockServerCodecCallbacks> codec_callbacks_;
  NiceMock<Network::MockServerConnection> mock_connection_;
  std::unique_ptr<Http1ServerCodec> codec_;
};

TEST_F(Http1ServerCodecTest, DecodeHeaderOnlyRequest) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Empty methods. Call these methods to increase coverage.
  codec_->onStatusImpl("", 0);
  codec_->onDecodingSuccess(ResponseHeaderFramePtr{nullptr}, {});

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 0\r\n"
             "custom: value\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        EXPECT_EQ(frame->frameFlags().endStream(), true);

        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestWithBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(body->buffer().length(), 4);
        EXPECT_EQ(body->buffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestAndCloseConnectionAfterHeader) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([this](RequestHeaderFramePtr, absl::optional<StartTime>) {
        mock_connection_.close(Network::ConnectionCloseType::NoFlush);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestAndCloseConnectionAfterBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([this](RequestCommonFramePtr) {
        mock_connection_.close(Network::ConnectionCloseType::NoFlush);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestWithChunkedBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"                          // Chunk header.
             "body"                           // Chunk body.
             "\r\n"                           // Chunk footer.
             "0\r\n"                          // Last chunk header.
             "trailer-key: trailer-value\r\n" // Trailers in the last chunk. Will be ignored.
             "\r\n");                         // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .Times(2)
      .WillRepeatedly(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        auto end_stream = frame->frameFlags().endStream();
        if (!end_stream) {
          EXPECT_EQ(body->buffer().length(), 4);
          EXPECT_EQ(body->buffer().toString(), "body");
        } else {
          EXPECT_EQ(body->buffer().length(), 0);
        }
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestWithChunkedBodyWithMultipleFrames) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"); // Chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);

        EXPECT_EQ(body->buffer().length(), 4);
        EXPECT_EQ(body->buffer().toString(), "body");
      }));

  codec_->decode(buffer, false);

  EXPECT_EQ(buffer.length(), 0);

  buffer.add("0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), true);
        EXPECT_EQ(body->buffer().length(), 0);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeUnexpectedRequest) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Connect.
  {
    initializeCodec();

    Buffer::OwnedImpl buffer;

    buffer.add("CONNECT host:443 HTTP/1.1\r\n"
               "custom: value\r\n"
               "\r\n");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Transfer-Encoding and Content-Length are set at same time.
  {
    initializeCodec();

    Buffer::OwnedImpl buffer;

    buffer.add("GET / HTTP/1.1\r\n"
               "Host: host\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Content-Length: 4\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Unknown Transfer-Encoding.
  {
    initializeCodec();

    Buffer::OwnedImpl buffer;

    buffer.add("GET / HTTP/1.1\r\n"
               "Host: host\r\n"
               "Transfer-Encoding: gzip, chunked\r\n" // Only 'chunked' is supported.
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Lost required request headers.
  {
    initializeCodec();

    Buffer::OwnedImpl buffer;

    buffer.add("GET / HTTP/1.1\r\n"
               "custom: value\r\n"
               "\r\n");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // HTTP1.0 request.
  {
    initializeCodec();

    Buffer::OwnedImpl buffer;

    buffer.add("GET / HTTP/1.0\r\n"
               "Host: host\r\n"
               "custom: value\r\n"
               "\r\n");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }
}

TEST_F(Http1ServerCodecTest, Respond) {
  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "GET");
  headers->addCopy(Http::LowerCaseString("custom"), "value");

  HttpRequestFrame request(std::move(headers), true);

  // Respond with a response.
  {
    for (int i = 0; i <= 20; i++) {
      auto response =
          codec_->respond(absl::Status(static_cast<absl::StatusCode>(i), ""), "", request);

      EXPECT_NE(response, nullptr);
      EXPECT_NE(dynamic_cast<HttpResponseFrame*>(response.get())->response_, nullptr);

      auto* response_headers = dynamic_cast<HttpResponseFrame*>(response.get())->response_.get();
      EXPECT_EQ(response_headers->getStatusValue(),
                std::to_string(Utility::statusToHttpStatus(static_cast<absl::StatusCode>(i))));
    }
  }
}

TEST_F(Http1ServerCodecTest, EncodeHeaderOnlyResponse) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);

  HttpResponseFrame response(std::move(headers), true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the response.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "HTTP/1.1 200 OK\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(response, encoding_context).ok());
  }
}

TEST_F(Http1ServerCodecTest, EncodeResponseWhichMissRequiredHeaders) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a request without method.
  auto headers = Http::ResponseHeaderMapImpl::create();

  HttpResponseFrame response(std::move(headers), true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    auto status_or = codec_->encode(response, encoding_context);
    EXPECT_FALSE(status_or.ok());
    EXPECT_EQ(status_or.status().message(), "missing required headers");
  }
}

TEST_F(Http1ServerCodecTest, EncodeResponseButNoActiveRequest) {
  // No active request.
  codec_->active_request_.reset();

  NiceMock<MockEncodingContext> encoding_context;

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  HttpResponseFrame response(std::move(headers), true);

  // Encode the response.
  {
    auto status_or = codec_->encode(response, encoding_context);
    EXPECT_FALSE(status_or.ok());
    EXPECT_EQ(status_or.status().message(), "no request for coming response");
  }
}

TEST_F(Http1ServerCodecTest, EncodeResponseWithBody) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->addCopy(Http::Headers::get().ContentLength, "4");

  HttpResponseFrame response(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the response.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "HTTP/1.1 200 OK\r\n"
                                       "content-length: 4\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(response, encoding_context).ok());

    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "body");

          buffer.drain(buffer.length());
        }));
    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}

TEST_F(Http1ServerCodecTest, EncodeResponseWithChunkedBody) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

  HttpResponseFrame response(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the response.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "HTTP/1.1 200 OK\r\n"
                                       "transfer-encoding: chunked\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(response, encoding_context).ok());

    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "4\r\n"  // Chunk header.
                                       "body"   // Chunk body.
                                       "\r\n"   // Chunk footer.
                                       "0\r\n"  // Last chunk header.
                                       "\r\n"); // Last chunk footer.
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}
TEST_F(Http1ServerCodecTest, EncodeResponseWithChunkedBodyButNotSetChunkHeader) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);

  HttpResponseFrame response(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the response.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(),
                    "HTTP/1.1 200 OK\r\n"
                    "transfer-encoding: chunked\r\n" // Codec will add this header for chunked
                                                     // response by default if it is not set.
                    "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(response, encoding_context).ok());

    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "4\r\n"  // Chunk header.
                                       "body"   // Chunk body.
                                       "\r\n"   // Chunk footer.
                                       "0\r\n"  // Last chunk header.
                                       "\r\n"); // Last chunk footer.
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}

TEST_F(Http1ServerCodecTest, DecodeRequestAndEncodeResponse) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Do repeated request and response.
  for (size_t i = 0; i < 100; i++) {
    Buffer::OwnedImpl buffer;

    buffer.add("GET / HTTP/1.1\r\n"
               "Host: host\r\n"
               "Transfer-Encoding: chunked\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
    EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_)).Times(2);

    codec_->decode(buffer, false);

    // Create a response.
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

    HttpResponseFrame response(std::move(headers), false);

    Buffer::OwnedImpl body_buffer("body");
    HttpRawBodyFrame body(body_buffer, true);

    NiceMock<MockEncodingContext> encoding_context;
    EXPECT_CALL(codec_callbacks_, writeToConnection(_)).Times(2);

    // Encode the response.
    EXPECT_TRUE(codec_->encode(response, encoding_context).ok());
    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}

TEST_F(Http1ServerCodecTest, DecodeExpectRequestAndItWillBeRepliedDirectly) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "custom: value\r\n"
             "Expect: 100-continue\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, writeToConnection(_)).WillOnce(Invoke([](Buffer::Instance& buffer) {
    EXPECT_EQ(buffer.toString(), "HTTP/1.1 100 Continue\r\n"
                                 "content-length: 0\r\n"
                                 "\r\n");
    buffer.drain(buffer.length());
  }));

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        EXPECT_EQ(frame->frameFlags().endStream(), true);

        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");

        // Expect header should be replied directly and then be removed from the request.
        EXPECT_FALSE(request->get("expect").has_value());
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, ResponseCompleteBeforeRequestComplete) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"); // Chunk footer. No last chunk header and footer, this is an incomplete
                      // request.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_));

  codec_->decode(buffer, false);

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

  HttpResponseFrame response(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  EXPECT_CALL(codec_callbacks_, writeToConnection(_));
  // Encode the response.
  EXPECT_TRUE(codec_->encode(response, encoding_context).ok());

  EXPECT_CALL(codec_callbacks_, writeToConnection(_));

  auto status_or = codec_->encode(body, encoding_context);
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().message(), "response complete before request complete");
}

TEST_F(Http1ServerCodecTest, NewRequestBeforeFirstRequestComplete) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"   // Chunk footer.
             "0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  Buffer::OwnedImpl buffer2;
  buffer2.add(buffer);

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_)).Times(2);

  codec_->decode(buffer, false);

  // First request is not complete, so the codec should close the connection.
  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer2, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestInSingleFrameMode) {
  initializeCodec(true, 8 * 1024 * 1024);

  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");

        EXPECT_EQ(request->optionalBuffer().length(), 4);
        EXPECT_EQ(request->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestInSingleFrameModeButBodyTooLarge1) {
  initializeCodec(true, 4);

  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 5\r\n"
             "custom: value\r\n"
             "\r\n"
             "body~"); // Request is complete and the body is too large.

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestInSingleFrameModeButBodyTooLarge2) {
  initializeCodec(true, 4);

  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Content-Length: 8\r\n"
             "custom: value\r\n"
             "\r\n"
             "xxxxx"); // Request is not complete but the body is too large.

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestWithChunkedBodyInSingleFrameMode) {
  initializeCodec(true, 8 * 1024 * 1024);

  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"   // Chunk footer.
             "0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");

        EXPECT_EQ(request->optionalBuffer().length(), 4);
        EXPECT_EQ(request->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, DecodeRequestWithChunkedBodyWithMultipleFramesInSingleFrameMode) {
  initializeCodec(true, 8 * 1024 * 1024);

  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("GET / HTTP/1.1\r\n"
             "Host: host\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"); // Chunk footer.

  codec_->decode(buffer, false);

  EXPECT_EQ(buffer.length(), 0);

  buffer.add("0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](RequestHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* request = dynamic_cast<HttpRequestFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(request->host(), "host");
        EXPECT_EQ(request->path(), "/");
        EXPECT_EQ(request->method(), "GET");
        EXPECT_EQ(request->get("host").value(), "host");
        EXPECT_EQ(request->get(":authority").value(), "host");
        EXPECT_EQ(request->get(":path").value(), "/");
        EXPECT_EQ(request->get(":method").value(), "GET");
        EXPECT_EQ(request->get("custom").value(), "value");

        EXPECT_EQ(request->optionalBuffer().length(), 4);
        EXPECT_EQ(request->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ServerCodecTest, EncodeResponseInSingleFrameMode) {
  // Mock request.
  codec_->active_request_ =
      ActiveRequest{nullptr, /*request_has_body_*/ false, /*request_complete_*/ true};

  // Create a response.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentLength(4);

  HttpResponseFrame response(std::move(headers), true);
  response.optionalBuffer().add("body");

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the response.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "HTTP/1.1 200 OK\r\n"
                                       "content-length: 4\r\n"
                                       "\r\n"
                                       "body");
          buffer.drain(buffer.length());
        }));
    auto status = codec_->encode(response, encoding_context);
    std::cout << status.status().message() << std::endl;
    EXPECT_TRUE(status.ok());
  }
}

class Http1ClientCodecTest : public testing::Test {
public:
  Http1ClientCodecTest() { initializeCodec(); }

  void initializeCodec(bool single_frame_mode = false, uint32_t max_buffer_size = 8 * 1024 * 1024) {
    codec_ = std::make_unique<Http1ClientCodec>(single_frame_mode, max_buffer_size);
    codec_->setCodecCallbacks(codec_callbacks_);
  }

  void encodingGetRequest() {
    ON_CALL(codec_callbacks_, connection())
        .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

    // Create a request.
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    headers->addCopy(Http::Headers::get().ContentLength, "4");

    HttpRequestFrame request(std::move(headers), false);

    Buffer::OwnedImpl body_buffer("body");
    HttpRawBodyFrame body(body_buffer, true);

    NiceMock<MockEncodingContext> encoding_context;

    // Encode the request.
    {
      EXPECT_CALL(codec_callbacks_, writeToConnection(_))
          .WillOnce(Invoke([](Buffer::Instance& buffer) {
            EXPECT_EQ(buffer.toString(), "GET /path HTTP/1.1\r\n"
                                         "host: host\r\n"
                                         "content-length: 4\r\n"
                                         "\r\n");
            buffer.drain(buffer.length());
          }));

      EXPECT_TRUE(codec_->encode(request, encoding_context).ok());

      EXPECT_CALL(codec_callbacks_, writeToConnection(_))
          .WillOnce(Invoke([](Buffer::Instance& buffer) {
            EXPECT_EQ(buffer.toString(), "body");
            buffer.drain(buffer.length());
          }));

      EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
    }
  }

  void encodingHeadRequest() {
    ON_CALL(codec_callbacks_, connection())
        .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

    // Create a request.
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "HEAD");
    headers->addCopy(Http::Headers::get().ContentLength, "0");

    HttpRequestFrame request(std::move(headers), true);

    NiceMock<MockEncodingContext> encoding_context;

    // Encode the request.
    {
      EXPECT_CALL(codec_callbacks_, writeToConnection(_))
          .WillOnce(Invoke([](Buffer::Instance& buffer) {
            EXPECT_EQ(buffer.toString(), "HEAD /path HTTP/1.1\r\n"
                                         "host: host\r\n"
                                         "content-length: 0\r\n"
                                         "\r\n");
            buffer.drain(buffer.length());
          }));

      EXPECT_TRUE(codec_->encode(request, encoding_context).ok());
    }
  }

  NiceMock<MockClientCodecCallbacks> codec_callbacks_;
  NiceMock<Network::MockServerConnection> mock_connection_;
  std::unique_ptr<Http1ClientCodec> codec_;
};

TEST_F(Http1ClientCodecTest, DecodeHeaderOnlyResponse) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Empty methods. Call these methods to increase coverage.
  codec_->onUrlImpl("", 0);
  codec_->onDecodingSuccess(RequestHeaderFramePtr{nullptr}, {});

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "content-length: 0\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        EXPECT_EQ(frame->frameFlags().endStream(), true);

        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());

        EXPECT_EQ(response->response_->getStatusValue(), "200");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, ResponseComesBeforeRequest) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "content-length: 0\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeHTTP10Response) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.0 200 OK\r\n"
             "content-length: 0\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseForHeadRequest) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingHeadRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 0\r\n"
             "custom: value\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), true);
        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->get("custom").value(), "value");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseShouldNotHasBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 304 OK\r\n"
             "Content-Length: 0\r\n"
             "custom: value\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), true);
        EXPECT_EQ(response->response_->getStatusValue(), "304");
        EXPECT_EQ(response->get("custom").value(), "value");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, Decode1xxResponseAndItWillBeIgnored) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;
  buffer.add("HTTP/1.1 100 Continue\r\n"
             "\r\n");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_)).Times(0);
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseWithBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);
        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->response_->getContentLengthValue(), "4");
        EXPECT_EQ(response->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](ResponseCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(body->buffer().length(), 4);
        EXPECT_EQ(body->buffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseAndCloseConnectionAfterHeader) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([this](ResponseHeaderFramePtr, absl::optional<StartTime>) {
        mock_connection_.close(Network::ConnectionCloseType::NoFlush);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseAndCloseConnectionAfterBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([this](ResponseCommonFramePtr) {
        mock_connection_.close(Network::ConnectionCloseType::NoFlush);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseWithChunkedBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"   // Chunk footer.
             "0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);
        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .Times(2)
      .WillRepeatedly(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        auto end_stream = frame->frameFlags().endStream();
        if (!end_stream) {
          EXPECT_EQ(body->buffer().length(), 4);
          EXPECT_EQ(body->buffer().toString(), "body");
        } else {
          EXPECT_EQ(body->buffer().length(), 0);
        }
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseWithChunkedBodyWithMultipleFrames) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"); // Chunk footer.
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);
        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->get("custom").value(), "value");
      }));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), false);

        EXPECT_EQ(body->buffer().length(), 4);
        EXPECT_EQ(body->buffer().toString(), "body");
      }));

  codec_->decode(buffer, false);

  EXPECT_EQ(buffer.length(), 0);

  buffer.add("0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_))
      .WillOnce(Invoke([](RequestCommonFramePtr frame) {
        auto* body = dynamic_cast<HttpRawBodyFrame*>(frame.get());
        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(body->buffer().length(), 0);
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeUnexpectedResponse) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Transfer-Encoding and Content-Length are set at same time.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 200 OK\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Content-Length: 4\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Unknown Transfer-Encoding.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 200 OK\r\n"
               "Transfer-Encoding: gzip, chunked\r\n" // Only 'chunked' is supported.
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Transfer-Encoding header for 204 response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 204 OK\r\n"
               "Transfer-Encoding: chunked\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Transfer-Encoding header for 1xx response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 100 Continue\r\n"
               "Transfer-Encoding: chunked\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Transfer-Encoding header for 304 response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 304 Not Modified\r\n"
               "Transfer-Encoding: chunked\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Non-zero Content-Length for 204 response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 204 OK\r\n"
               "Content-Length: 4\r\n"
               "custom: value\r\n"
               "\r\n"
               "body");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Non-zero Content-Length for 1xx response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 100 Continue\r\n"
               "Content-Length: 4\r\n"
               "custom: value\r\n"
               "\r\n"
               "body");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }

  // Non-zero Content-Length for 304 response.
  {
    initializeCodec();

    encodingGetRequest();

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 304 Not Modified\r\n"
               "Content-Length: 4\r\n"
               "custom: value\r\n"
               "\r\n"
               "body");

    EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
    codec_->decode(buffer, false);
  }
}

TEST_F(Http1ClientCodecTest, EncodeHeaderOnlyRequest) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "GET");
  headers->addCopy(Http::LowerCaseString("custom"), "value");

  HttpRequestFrame request(std::move(headers), true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "GET /path HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "custom: value\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(request, encoding_context).ok());
  }
}

TEST_F(Http1ClientCodecTest, EncodeRequestMissRequiredHeaders) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Create a request without method.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::LowerCaseString("custom"), "value");

  HttpRequestFrame request(std::move(headers), true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    auto status_or = codec_->encode(request, encoding_context);
    EXPECT_FALSE(status_or.ok());
    EXPECT_EQ(status_or.status().message(), "missing required headers");
  }
}

TEST_F(Http1ClientCodecTest, EncodeRequestWithBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "POST");
  headers->addCopy(Http::Headers::get().ContentLength, "4");

  HttpRequestFrame request(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "POST /path HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "content-length: 4\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(request, encoding_context).ok());

    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "body");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}

TEST_F(Http1ClientCodecTest, EncodeRequestWithChunkdBody) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "GET");
  headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

  HttpRequestFrame request(std::move(headers), false);

  Buffer::OwnedImpl body_buffer("body");
  HttpRawBodyFrame body(body_buffer, true);

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "GET /path HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "transfer-encoding: chunked\r\n"
                                       "\r\n");
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(request, encoding_context).ok());

    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "4\r\n"  // Chunk header.
                                       "body"   // Chunk body.
                                       "\r\n"   // Chunk footer.
                                       "0\r\n"  // Last chunk header.
                                       "\r\n"); // Last chunk footer.
          buffer.drain(buffer.length());
        }));

    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());
  }
}

TEST_F(Http1ClientCodecTest, EncodeRequestAndDecodeResponse) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Do repeated request and response.
  for (size_t i = 0; i < 100; i++) {
    // Create a request.
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->addCopy(Http::Headers::get().HostLegacy, "host");
    headers->addCopy(Http::Headers::get().Path, "/path");
    headers->addCopy(Http::Headers::get().Method, "GET");
    headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

    HttpRequestFrame request(std::move(headers), false);

    Buffer::OwnedImpl body_buffer("body");
    HttpRawBodyFrame body(body_buffer, true);

    NiceMock<MockEncodingContext> encoding_context;
    EXPECT_CALL(codec_callbacks_, writeToConnection(_)).Times(2);

    // Encode the request.
    EXPECT_TRUE(codec_->encode(request, encoding_context).ok());
    EXPECT_TRUE(codec_->encode(body, encoding_context).ok());

    Buffer::OwnedImpl buffer;

    buffer.add("HTTP/1.1 200 OK\r\n"
               "Transfer-Encoding: chunked\r\n"
               "custom: value\r\n"
               "\r\n"
               "4\r\n"  // Chunk header.
               "body"   // Chunk body.
               "\r\n"   // Chunk footer.
               "0\r\n"  // Last chunk header.
               "\r\n"); // Last chunk footer.

    // Decode the response.
    EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
    EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_)).Times(2);

    codec_->decode(buffer, false);
  }
}

TEST_F(Http1ClientCodecTest, ResponseCompleteBeforeRequestComplete) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "GET");
  headers->addCopy(Http::Headers::get().TransferEncoding, "chunked");

  HttpRequestFrame request(std::move(headers), false);

  NiceMock<MockEncodingContext> encoding_context;
  EXPECT_CALL(codec_callbacks_, writeToConnection(_));

  // Encode the request. Only the headers are encoded and the body is not encoded.
  EXPECT_TRUE(codec_->encode(request, encoding_context).ok());

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"   // Chunk footer.
             "0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  // Decode the response.
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _));
  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_)).Times(2);

  // Finally, the onDecodingFailure(_) is called because the request is not complete and the
  // response is complete.
  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, EncodeRequestInSingleFrameMode) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 8 * 1024 * 1024);

  // Create a request.
  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addCopy(Http::Headers::get().HostLegacy, "host");
  headers->addCopy(Http::Headers::get().Path, "/path");
  headers->addCopy(Http::Headers::get().Method, "GET");
  headers->setContentLength(4);

  HttpRequestFrame request(std::move(headers), true);
  request.optionalBuffer().add("body");

  NiceMock<MockEncodingContext> encoding_context;

  // Encode the request.
  {
    EXPECT_CALL(codec_callbacks_, writeToConnection(_))
        .WillOnce(Invoke([](Buffer::Instance& buffer) {
          EXPECT_EQ(buffer.toString(), "GET /path HTTP/1.1\r\n"
                                       "host: host\r\n"
                                       "content-length: 4\r\n"
                                       "\r\n"
                                       "body");
          buffer.drain(buffer.length());
        }));
    EXPECT_TRUE(codec_->encode(request, encoding_context).ok());
  }
}

TEST_F(Http1ClientCodecTest, DecodeResponseInSingleFrameMode) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 8 * 1024 * 1024);

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 4\r\n"
             "custom: value\r\n"
             "\r\n"
             "body");

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);
        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->response_->getContentLengthValue(), "4");
        EXPECT_EQ(response->get("custom").value(), "value");

        EXPECT_EQ(response->optionalBuffer().length(), 4);
        EXPECT_EQ(response->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseInSingleFrameModeButBodyIsTooLarge1) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 4);

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 5\r\n"
             "custom: value\r\n"
             "\r\n"
             "body~"); // The response is complete and the body is too large.

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseInSingleFrameModeButBodyIsTooLarge2) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 4);

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Content-Length: 8\r\n"
             "custom: value\r\n"
             "\r\n"
             "xxxxx"); // The response is not complete but the body is too large.

  EXPECT_CALL(codec_callbacks_, onDecodingFailure(_));
  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseWithChunkedBodyInSingleFrameMode) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 8 * 1024 * 1024);

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"   // Chunk footer.
             "0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->get("custom").value(), "value");

        EXPECT_EQ(response->optionalBuffer().length(), 4);
        EXPECT_EQ(response->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST_F(Http1ClientCodecTest, DecodeResponseWithChunkedBodyWithMultipleFramesInSingleFrameMode) {
  ON_CALL(codec_callbacks_, connection())
      .WillByDefault(testing::Return(makeOptRef<Network::Connection>(mock_connection_)));

  initializeCodec(true, 8 * 1024 * 1024);

  encodingGetRequest();

  Buffer::OwnedImpl buffer;

  buffer.add("HTTP/1.1 200 OK\r\n"
             "Transfer-Encoding: chunked\r\n"
             "custom: value\r\n"
             "\r\n"
             "4\r\n"  // Chunk header.
             "body"   // Chunk body.
             "\r\n"); // Chunk footer.

  codec_->decode(buffer, false);

  EXPECT_EQ(buffer.length(), 0);

  buffer.add("0\r\n"  // Last chunk header.
             "\r\n"); // Last chunk footer.

  EXPECT_CALL(codec_callbacks_, onDecodingSuccess(_, _))
      .WillOnce(Invoke([](ResponseHeaderFramePtr frame, absl::optional<StartTime>) {
        auto* response = dynamic_cast<HttpResponseFrame*>(frame.get());

        EXPECT_EQ(frame->frameFlags().endStream(), true);

        EXPECT_EQ(response->response_->getStatusValue(), "200");
        EXPECT_EQ(response->get("custom").value(), "value");

        EXPECT_EQ(response->optionalBuffer().length(), 4);
        EXPECT_EQ(response->optionalBuffer().toString(), "body");
      }));

  codec_->decode(buffer, false);
}

TEST(Http1CodecFactoryTest, Http1CodecFactoryTest) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;
  ProtoConfig proto_config;

  Http1CodecFactoryConfig config_factory;

  auto codec_factory = config_factory.createCodecFactory(proto_config, context);

  auto client_factory = codec_factory->createClientCodec();
  auto server_factory = codec_factory->createServerCodec();

  EXPECT_NE(dynamic_cast<Http1ClientCodec*>(client_factory.get()), nullptr);
  EXPECT_NE(dynamic_cast<Http1ServerCodec*>(server_factory.get()), nullptr);
}

} // namespace
} // namespace Http1
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
