#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/http/codes.h"

#include "source/common/http/header_utility.h"
#include "source/common/local_reply/local_reply.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
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
  LocalReplyTest() : stream_info_(time_system_.timeSystem(), nullptr) { resetData(TestInitCode); }

  void resetData(Http::Code code) {
    code_ = code;
    body_ = TestInitBody;
    content_type_ = TestInitContentType;
  }
  void resetData(uint32_t code) { resetData(static_cast<Http::Code>(code)); }

  Http::Code code_;
  std::string body_;
  absl::string_view content_type_;

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Event::SimulatedTimeSystem time_system_;
  StreamInfo::StreamInfoImpl stream_info_;

  envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(LocalReplyTest, TestEmptyConfig) {
  // Empty LocalReply config.
  auto local = Factory::create(config_, context_);

  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(response_headers_.Status()->value().getStringView(),
            std::to_string(enumToInt(TestInitCode)));
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestDefaultLocalReply) {
  // Default LocalReply should be the same as empty config.
  auto local = Factory::createDefault();

  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(response_headers_.Status()->value().getStringView(),
            std::to_string(enumToInt(TestInitCode)));
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestInvalidConfigEmptyFilter) {
  // Invalid config: a mapper should have a valid filter
  const std::string yaml = R"(
    mappers:
    - status_code: 401
)";
  TestUtility::loadFromYaml(yaml, config_);

  std::string err;
  EXPECT_FALSE(Validate(config_, &err));
}

TEST_F(LocalReplyTest, TestInvalidConfigStatusCode) {
  // Invalid config: status_code should be at range [200, 600)
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 400
              runtime_key: key_b
      status_code: 100
)";
  TestUtility::loadFromYaml(yaml, config_);

  std::string err;
  EXPECT_FALSE(Validate(config_, &err));
}

TEST_F(LocalReplyTest, TestDefaultTextFormatter) {
  // Default text formatter without any mappers
  const std::string yaml = R"(
  body_format:
     text_format_source:
       inline_string: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(response_headers_.Status()->value().getStringView(),
            std::to_string(enumToInt(TestInitCode)));
  EXPECT_EQ(body_, "Init body text 200");
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestDefaultJsonFormatter) {
  // Default json formatter without any mappers
  const std::string yaml = R"(
  body_format:
    json_format:
      text: "plain text"
      path: "%REQ(:path)%"
      code: "%RESPONSE_CODE%"
      body: "%LOCAL_REPLY_BODY%"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(response_headers_.Status()->value().getStringView(),
            std::to_string(enumToInt(TestInitCode)));
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
      status_code: 401
      body:
        inline_string: "400 body text"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 403
              runtime_key: key_b
      body:
        inline_string: ""
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 410
              runtime_key: key_b
      body:
        inline_string: "410 body text"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 420
              runtime_key: key_b
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

  // code=400 matches the first filter; rewrite code and body
  resetData(400);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(401));
  EXPECT_EQ(stream_info_.response_code_, 401U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "401");
  EXPECT_EQ(body_, "400 body text");
  EXPECT_EQ(content_type_, "text/plain");

  // code=403 matches the second filter; does not rewrite code, sets an empty body and content_type.
  resetData(403);
  body_ = "original body text";
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(403));
  EXPECT_EQ(stream_info_.response_code_, 403U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "403");
  EXPECT_EQ(body_, "");
  EXPECT_EQ(content_type_, "text/plain");

  // code=410 matches the third filter; rewrite body only
  resetData(410);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(410));
  EXPECT_EQ(stream_info_.response_code_, 410U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "410");
  EXPECT_EQ(body_, "410 body text");
  EXPECT_EQ(content_type_, "text/plain");

  // code=420 matches the fourth filter; rewrite code only
  resetData(420);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(421));
  EXPECT_EQ(stream_info_.response_code_, 421U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "421");
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");

  // code=430 matches the fifth filter; rewrite nothing
  resetData(430);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(430));
  EXPECT_EQ(stream_info_.response_code_, 430U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "430");
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");
}

// Test that we can use the deprecated `text_format` field with an empty value,
// which is currently the only use case that it serves until we relax the
// size validation on `DataSource.inline_string`.
TEST_F(LocalReplyTest, DEPRECATED_FEATURE_TEST(TestMapperRewriteDeprecatedTextFormatEmpty)) {
  // Match with response_code, and rewrite the code and body.
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 404
              runtime_key: key_b
      body_format_override:
        text_format: ""
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  // code=404 matches the only filter; does not rewrite code, sets an empty body and content_type.
  resetData(404);
  body_ = "original body text";
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(404));
  EXPECT_EQ(stream_info_.response_code_, 404U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "404");
  EXPECT_EQ(body_, "");
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
      status_code: 401
      body:
        inline_string: "401 body text"
      body_format_override:
        json_format:
          text: "401 filter formatter"
          path: "%REQ(:path)%"
          code: "%RESPONSE_CODE%"
          body: "%LOCAL_REPLY_BODY%"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 410
              runtime_key: key_b
      status_code: 411
      body:
        inline_string: "411 body text"
    body_format:
      text_format_source:
        inline_string: "%LOCAL_REPLY_BODY% %RESPONSE_CODE% default formatter"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  // code=400 matches the first filter; rewrite code and body
  // has its own formatter
  resetData(400);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(401));
  EXPECT_EQ(stream_info_.response_code_, 401U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "401");
  EXPECT_EQ(content_type_, "application/json");

  const std::string expected = R"({
    "text": "401 filter formatter",
    "path": "/bar/foo",
    "code": 401,
    "body": "401 body text"
})";
  EXPECT_TRUE(TestUtility::jsonStringEqual(body_, expected));

  // code=410 matches the second filter; rewrite code and body
  // but using default formatter
  resetData(410);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(411));
  EXPECT_EQ(stream_info_.response_code_, 411U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "411");
  EXPECT_EQ(body_, "411 body text 411 default formatter");
  EXPECT_EQ(content_type_, "text/plain");
}

TEST_F(LocalReplyTest, TestHeaderAddition) {
  // Default text formatter without any mappers
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: GE
            value:
              default_value: 0
              runtime_key: key_b
      headers_to_add:
        - header:
            key: foo-1
            value: bar1
          append_action: APPEND_IF_EXISTS_OR_ADD
        - header:
            key: foo-2
            value: override-bar2
          append_action: OVERWRITE_IF_EXISTS_OR_ADD
        - header:
            key: foo-3
            value: append-bar3
          append_action: APPEND_IF_EXISTS_OR_ADD
        - header:
            key: local-reply-req-id
            value: '%REQ(req-id)%'
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  response_headers_.addCopy("foo-2", "bar2");
  response_headers_.addCopy("foo-3", "bar3");
  Http::TestRequestHeaderMapImpl request_headers_with_req_id{{"req-id", "123"}};
  local->rewrite(&request_headers_with_req_id, response_headers_, stream_info_, code_, body_,
                 content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(content_type_, "text/plain");

  EXPECT_EQ(response_headers_.get_("foo-1"), "bar1");
  EXPECT_EQ(response_headers_.get_("foo-2"), "override-bar2");
  EXPECT_EQ(response_headers_.get_("local-reply-req-id"), "123");
  const auto out = response_headers_.get(Http::LowerCaseString("foo-3"));
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0]->value().getStringView(), "bar3");
  ASSERT_EQ(out[1]->value().getStringView(), "append-bar3");
}

TEST_F(LocalReplyTest, TestMapperWithContentType) {
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
      status_code: 401
      body:
        inline_string: "401 body text"
      body_format_override:
        text_format_source:
          inline_string: "<h1>%LOCAL_REPLY_BODY%</h1>"
        content_type: "text/html; charset=UTF-8"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 410
              runtime_key: key_b
      status_code: 411
      body:
        inline_string: "411 body text"
    - filter:
        status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 420
              runtime_key: key_b
      status_code: 421
      body:
        inline_string: "421 body text"
      body_format_override:
        text_format_source:
          inline_string: "%LOCAL_REPLY_BODY%"
    body_format:
      text_format_source:
        inline_string: "<h1>%LOCAL_REPLY_BODY%</h1> %RESPONSE_CODE% default formatter"
      content_type: "text/html; charset=UTF-8"
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  // code=400 matches the first filter; rewrite code and body
  // has its own formatter.
  // content-type is explicitly set to text/html; charset=UTF-8.
  resetData(400);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(401));
  EXPECT_EQ(stream_info_.response_code_, 401U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "401");
  EXPECT_EQ(body_, "<h1>401 body text</h1>");
  EXPECT_EQ(content_type_, "text/html; charset=UTF-8");

  // code=410 matches the second filter; rewrite code and body
  // but using default formatter.
  // content-type is explicitly set to text/html; charset=UTF-8.
  resetData(410);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(411));
  EXPECT_EQ(stream_info_.response_code_, 411U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "411");
  EXPECT_EQ(body_, "<h1>411 body text</h1> 411 default formatter");
  EXPECT_EQ(content_type_, "text/html; charset=UTF-8");

  // code=420 matches the third filter; rewrite code and body
  // has its own formatter.
  // default content-type is set based on reply format type.
  resetData(420);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(421));
  EXPECT_EQ(stream_info_.response_code_, 421U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "421");
  EXPECT_EQ(body_, "421 body text");
  EXPECT_EQ(content_type_, "text/plain");
}

} // namespace LocalReply
} // namespace Envoy
