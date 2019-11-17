#include "envoy/http/codes.h"

#include "common/local_reply/local_reply.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

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
  Http::Code code_{Http::Code::OK};
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

TEST_F(LocalReplyTest, ShouldMatchFirstFilterAndReturn) {
  // given
  std::list<ResponseMapperPtr> mappers;
  // first mapper
  absl::optional<uint32_t> response_code1(504);
  ResponseRewriterPtr response_rewriter1 = std::make_unique<ResponseRewriter>(response_code1);
  AccessLog::MockFilter* filter1{new AccessLog::MockFilter()};
  ResponseMapperPtr mapper1 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter1}, std::move(response_rewriter1)});
  mappers.emplace_back(std::move(mapper1));
  EXPECT_CALL(*filter1, evaluate(_, _, _, _)).WillOnce(Return(true));

  // second mapper
  absl::optional<uint32_t> response_code2(505);
  ResponseRewriterPtr response_rewriter2 = std::make_unique<ResponseRewriter>(response_code2);
  AccessLog::MockFilter* filter2{new AccessLog::MockFilter()};
  ResponseMapperPtr mapper2 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter2}, std::move(response_rewriter2)});
  mappers.emplace_back(std::move(mapper2));
  EXPECT_CALL(*filter2, evaluate(_, _, _, _)).Times(0);

  LocalReplyPtr local_reply = std::make_unique<LocalReply>(LocalReply{
      std::move(mappers), std::move(formatter_), Http::Headers::get().ContentTypeValues.Text});

  // when
  local_reply->matchAndRewrite(&headers_, &headers_, &headers_, stream_info_, code_);

  // then
  EXPECT_EQ(code_, Http::Code::GatewayTimeout);
}

TEST_F(LocalReplyTest, ShouldMatchSecondFilterAndReturn) {
  // given
  std::list<ResponseMapperPtr> mappers;

  // first mapper
  AccessLog::MockFilter* filter1{new AccessLog::MockFilter()};
  absl::optional<uint32_t> response_code1(504);
  ResponseRewriterPtr response_rewriter1 = std::make_unique<ResponseRewriter>(response_code1);
  ResponseMapperPtr mapper1 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter1}, std::move(response_rewriter1)});
  mappers.emplace_back(std::move(mapper1));
  EXPECT_CALL(*filter1, evaluate(_, _, _, _)).WillOnce(Return(false));

  // second mapper
  AccessLog::MockFilter* filter2{new AccessLog::MockFilter()};
  absl::optional<uint32_t> response_code2(505);
  ResponseRewriterPtr response_rewriter2 = std::make_unique<ResponseRewriter>(response_code2);
  ResponseMapperPtr mapper2 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter2}, std::move(response_rewriter2)});
  mappers.emplace_back(std::move(mapper2));
  EXPECT_CALL(*filter2, evaluate(_, _, _, _)).WillOnce(Return(true));

  LocalReplyPtr local_reply = std::make_unique<LocalReply>(LocalReply{
      std::move(mappers), std::move(formatter_), Http::Headers::get().ContentTypeValues.Text});

  // when
  local_reply->matchAndRewrite(&headers_, &headers_, &headers_, stream_info_, code_);

  // then
  EXPECT_EQ(code_, Http::Code::HTTPVersionNotSupported);
}

TEST_F(LocalReplyTest, ShuldNotMatchAnyFilter) {
  // given
  std::list<ResponseMapperPtr> mappers;
  // first mapper
  absl::optional<uint32_t> response_code1(504);
  ResponseRewriterPtr response_rewriter1 = std::make_unique<ResponseRewriter>(response_code1);
  AccessLog::MockFilter* filter1{new AccessLog::MockFilter()};
  ResponseMapperPtr mapper1 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter1}, std::move(response_rewriter1)});
  mappers.emplace_back(std::move(mapper1));
  EXPECT_CALL(*filter1, evaluate(_, _, _, _)).WillOnce(Return(false));

  // second mapper
  absl::optional<uint32_t> response_code2(505);
  ResponseRewriterPtr response_rewriter2 = std::make_unique<ResponseRewriter>(response_code2);
  AccessLog::MockFilter* filter2{new AccessLog::MockFilter()};
  ResponseMapperPtr mapper2 = std::make_unique<ResponseMapper>(
      ResponseMapper{AccessLog::FilterPtr{filter2}, std::move(response_rewriter2)});
  mappers.emplace_back(std::move(mapper2));
  EXPECT_CALL(*filter2, evaluate(_, _, _, _)).WillOnce(Return(false));

  LocalReplyPtr local_reply = std::make_unique<LocalReply>(LocalReply{
      std::move(mappers), std::move(formatter_), Http::Headers::get().ContentTypeValues.Text});

  // when
  local_reply->matchAndRewrite(&headers_, &headers_, &headers_, stream_info_, code_);

  // then
  EXPECT_EQ(code_, Http::Code::OK);
}

} // namespace LocalReply
} // namespace Envoy