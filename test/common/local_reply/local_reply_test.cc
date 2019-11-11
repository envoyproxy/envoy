#include "envoy/http/codes.h"

#include "common/local_reply/local_reply.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"

using testing::_;

namespace Envoy {
namespace LocalReply {

class ResponseRewriterTest : public testing::Test {
public:
  ResponseRewriterTest() {}
};

TEST_F(ResponseRewriterTest, ShouldChangeResponseCode) {
  // given
  absl::optional<uint32_t> response_code(504);
  ResponseRewriterPtr response_rewriter = std::make_unique<ResponseRewriter>(response_code);
  Http::Code code{Http::Code::OK};

  // when
  response_rewriter->rewrite(code);

  // then
  EXPECT_EQ(code, Http::Code::GatewayTimeout);
}

TEST_F(ResponseRewriterTest, ShouldNotChangeResponseCode) {
  // given
  ResponseRewriterPtr response_rewriter = std::make_unique<ResponseRewriter>(absl::nullopt);
  Http::Code code{Http::Code::OK};

  // when
  response_rewriter->rewrite(code);

  // then
  EXPECT_EQ(code, Http::Code::OK);
}

class LocalReplyTest : public testing::Test {
public:
  LocalReplyTest() { formatter_ = std::make_unique<Envoy::AccessLog::FormatterImpl>("plain"); }

  Envoy::AccessLog::FormatterPtr formatter_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Http::TestHeaderMapImpl headers_;
  Http::Code code_;
};

TEST_F(LocalReplyTest, ShouldInsertContentLengthAndTypeHeaders) {
  // given
  LocalReplyPtr local_reply = std::make_unique<LocalReply>(
      LocalReply{{}, std::move(formatter_), Http::Headers::get().ContentTypeValues.Text});
  absl::string_view body = "test body";

  // when
  local_reply->insertContentHeaders(body, &headers_);
  // then

  EXPECT_EQ(headers_.ContentLength()->value().getStringView(), "9");
  EXPECT_EQ(headers_.ContentType()->value().getStringView(), "text/plain");
}

TEST_F(LocalReplyTest, ShouldNotInsertContentHeadersForEmptyBody) {
  // given
  LocalReplyPtr local_reply = std::make_unique<LocalReply>(
      LocalReply{{}, std::move(formatter_), Http::Headers::get().ContentTypeValues.Text});
  absl::string_view body = {};

  // when
  local_reply->insertContentHeaders(body, &headers_);

  // then
  EXPECT_TRUE(headers_.ContentLength() == nullptr);
  EXPECT_TRUE(headers_.ContentType() == nullptr);
}
} // namespace LocalReply
} // namespace Envoy