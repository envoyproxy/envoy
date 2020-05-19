#include "envoy/http/codes.h"

#include "common/local_reply/local_reply.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace LocalReply {
namespace {

const Http::Code TestInitCode = Http::Code::OK;
const std::string TestInitBody = "Init body text";
const absl::string_view TestInitContentType = "content-type";
} // namespace

class LocalReplyTest : public testing::Test {
public:
  LocalReplyTest() : stream_info_(time_system_.timeSystem()) {
    resetData();
    stream_info_.response_code_ = static_cast<uint32_t>(code_);
  }

  void resetData() {
    code_ = TestInitCode;
    body_ = TestInitBody;
    content_type_ = TestInitContentType;
  }

  Http::Code code_;
  std::string body_;
  absl::string_view content_type_;

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}};
  Event::SimulatedTimeSystem time_system_;
  StreamInfo::StreamInfoImpl stream_info_;

  envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(LocalReplyTest, TestEmptyConfig) {
  // Empty LocalReply config.
  auto local = Factory::create(config_, context_);

  local->rewrite(nullptr, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestDefaultTextFormatter) {
  // Default text formatter without any mappers
  const std::string yaml = R"(
  format:
     text_format: "%RESP_BODY% %RESPONSE_CODE%"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  local->rewrite(nullptr, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(body_, "Init body text 200");
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestDefaultJsonFormatter) {
  // Default json formatter without any mappers
  const std::string yaml = R"(
  format:
    json_format:
      text: "plain text"
      path: "%REQ(:path)%"
      code: "%RESPONSE_CODE%"
      body: "%RESP_BODY%"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(content_type_, "application/json");

  const std::string expected = R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200,
    "body": "Init body text"
})";
  EXPECT_TRUE(TestUtility::jsonStringEqual(body_, expected));
}

TEST_F(LocalReplyTest, TestMapperRewrite) {
  // Match with response_code, and rewrite the code and body.
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 400
              runtime_key: key_b
      rewriter:
         status_code: 401
         body:
           inline_string: "400 body text"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 410
              runtime_key: key_b
      rewriter:
         body:
           inline_string: "410 body text"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 420
              runtime_key: key_b
      rewriter:
         status_code: 421
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 430
              runtime_key: key_b
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  // response_code=400 matches the first filter; rewrite code and body
  stream_info_.response_code_ = 400;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(401));
  EXPECT_EQ(body_, "400 body text");
  EXPECT_EQ(content_type_, "text/plain");

  // response_code=410 matches the second filter; rewrite body only
  resetData();
  stream_info_.response_code_ = 410;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(body_, "410 body text");
  EXPECT_EQ(content_type_, "text/plain");

  // response_code=420 matches the third filter; rewrite code only
  resetData();
  stream_info_.response_code_ = 420;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(421));
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");

  // response_code=430 matches the fourth filter; rewrite nothing
  resetData();
  stream_info_.response_code_ = 430;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestMapperFormat) {
  // Match with response_code, and rewrite the code and body.
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 400
              runtime_key: key_b
      rewriter:
         status_code: 401
         body:
           inline_string: "401 body text"
      format:
        json_format:
          text: "401 filter formatter"
          path: "%REQ(:path)%"
          code: "%RESPONSE_CODE%"
          body: "%RESP_BODY%"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 410
              runtime_key: key_b
      rewriter:
         status_code: 411
         body:
           inline_string: "411 body text"
    format:
      text_format: "%RESP_BODY% %RESPONSE_CODE% default formatter"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  // response_code=400 matches the first filter; rewrite code and body
  // has its own formatter
  stream_info_.response_code_ = 400;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(401));
  EXPECT_EQ(content_type_, "application/json");

  const std::string expected = R"({
    "text": "401 filter formatter",
    "path": "/bar/foo",
    "code": 401,
    "body": "401 body text"
})";
  EXPECT_TRUE(TestUtility::jsonStringEqual(body_, expected));

  // response_code=410 matches the second filter; rewrite code and body
  // but using default formatter
  resetData();
  stream_info_.response_code_ = 410;
  local->rewrite(&request_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(411));
  EXPECT_EQ(body_, "411 body text 411 default formatter");
  EXPECT_EQ(content_type_, "text/plain");
}

} // namespace LocalReply
} // namespace Envoy
