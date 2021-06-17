#include "source/common/http/http1/parser_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

// No-op parser callbacks.
class TestParserCallbacks : public ParserCallbacks {
  Status onMessageBegin() override { return okStatus(); };
  Status onUrl(const char*, size_t) override { return okStatus(); };
  Status onHeaderField(const char*, size_t) override { return okStatus(); };
  Status onHeaderValue(const char*, size_t) override { return okStatus(); };
  Envoy::StatusOr<ParserStatus> onHeadersComplete() override { return ParserStatus::Success; };
  void bufferBody(const char*, size_t) override{};
  StatusOr<ParserStatus> onMessageComplete() override { return ParserStatus::Success; };
  void onChunkHeader(bool) override{};

  int setAndCheckCallbackStatus(Status&&) override { return 0; };
  int setAndCheckCallbackStatusOr(Envoy::StatusOr<ParserStatus>&&) override { return 0; };
};

class RequestParserImplTest : public testing::Test {
public:
  RequestParserImplTest() {
    parser_ = std::make_unique<HttpParserImpl>(MessageType::Request, &callbacks_);
  }
  TestParserCallbacks callbacks_;
  std::unique_ptr<HttpParserImpl> parser_;
};

TEST_F(RequestParserImplTest, TestExecute) {
  const char* request = "GET / HTTP/1.1\r\n\r\n";
  int request_len = strlen(request);

  auto [nread, rc] = parser_->execute(request, request_len);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(parser_->methodName(), "GET");
  EXPECT_EQ(parser_->httpMajor(), 1);
  EXPECT_EQ(parser_->httpMinor(), 1);
}

TEST_F(RequestParserImplTest, TestContentLength) {
  const char* request = "POST / HTTP/1.1\r\nContent-Length: 003\r\n\r\n";
  int request_len = strlen(request);

  auto [nread, rc] = parser_->execute(request, request_len);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(parser_->contentLength().has_value());
  EXPECT_EQ(parser_->contentLength().value(), 3);
}

TEST_F(RequestParserImplTest, TestErrorName) {
  // Duplicate Content-Length causes error.
  const char* request = "POST / HTTP/1.1\r\nContent-Length: 003\r\nContent-Length: 001\r\n";
  int request_len = strlen(request);
  auto [nread, rc] = parser_->execute(request, request_len);
  EXPECT_NE(rc, 0);
  EXPECT_EQ(parser_->errnoName(rc), "HPE_UNEXPECTED_CONTENT_LENGTH");
}

TEST_F(RequestParserImplTest, TestChunked) {
  const char* request = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\na\r\n0\r\n\r\n";
  int request_len = strlen(request);
  auto [nread, rc] = parser_->execute(request, request_len);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(parser_->hasTransferEncoding());
  EXPECT_TRUE(parser_->isChunked());
}

class ResponseParserImplTest : public testing::Test {
public:
  ResponseParserImplTest() {
    parser_ = std::make_unique<HttpParserImpl>(MessageType::Response, &callbacks_);
  }
  TestParserCallbacks callbacks_;
  std::unique_ptr<HttpParserImpl> parser_;
};

TEST_F(ResponseParserImplTest, TestStatus) {
  const char* response = "HTTP/1.1 200 OK\r\n\r\n";
  int response_len = strlen(response);
  auto [nread, rc] = parser_->execute(response, response_len);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(parser_->statusCode(), 200);
}

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
