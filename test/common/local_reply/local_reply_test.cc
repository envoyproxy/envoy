#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/http/codes.h"

#include "common/http/header_utility.h"
#include "common/local_reply/local_reply.h"

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
  LocalReplyTest() : stream_info_(time_system_.timeSystem()) { resetData(TestInitCode); }

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
     text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
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

  // code=410 matches the second filter; rewrite body only
  resetData(410);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(410));
  EXPECT_EQ(stream_info_.response_code_, 410U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "410");
  EXPECT_EQ(body_, "410 body text");
  EXPECT_EQ(content_type_, "text/plain");

  // code=420 matches the third filter; rewrite code only
  resetData(420);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(421));
  EXPECT_EQ(stream_info_.response_code_, 421U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "421");
  EXPECT_EQ(body_, TestInitBody);
  EXPECT_EQ(content_type_, "text/plain");

  // code=430 matches the fourth filter; rewrite nothing
  resetData(430);
  local->rewrite(&request_headers_, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, static_cast<Http::Code>(430));
  EXPECT_EQ(stream_info_.response_code_, 430U);
  EXPECT_EQ(response_headers_.Status()->value().getStringView(), "430");
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
      text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE% default formatter"
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
          append: true
        - header:
            key: foo-2
            value: override-bar2
          append: false
        - header:
            key: foo-3
            value: append-bar3
          append: true
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  response_headers_.addCopy("foo-2", "bar2");
  response_headers_.addCopy("foo-3", "bar3");
  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(content_type_, "text/plain");

  EXPECT_EQ(response_headers_.get_("foo-1"), "bar1");
  EXPECT_EQ(response_headers_.get_("foo-2"), "override-bar2");
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
        text_format: "<h1>%LOCAL_REPLY_BODY%</h1>"
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
        text_format: "%LOCAL_REPLY_BODY%"
    body_format:
      text_format: "<h1>%LOCAL_REPLY_BODY%</h1> %RESPONSE_CODE% default formatter"
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

// Test addition of tokenized headers to local reply
TEST_F(LocalReplyTest, TestTokenizedHeadersAddition) {
  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: GE
            value:
              default_value: 0
              runtime_key: key_b
      tokenized_headers_to_add:
        - name: tokenized-foo-1
          headers:
            - key: foo-1
              value: bar1
            - key: foo-2
              value: bar2  
          append: true
        - name: tokenized-foo-2
          headers:
            - key: foo-3
              value: bar3
            - key: foo-4
              value: bar4  
          append: false
        - name: tokenized-foo-3
          headers:
            - key: foo-5
              value: bar5
            - key: foo-6
              value: bar6  
          append: true    
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  response_headers_.addCopy("tokenized-foo-2", "original2");
  response_headers_.addCopy("tokenized-foo-3", "original3");
  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(content_type_, "text/plain");

  EXPECT_EQ(response_headers_.get_("tokenized-foo-1"), "foo-1;bar1,foo-2;bar2");
  EXPECT_EQ(response_headers_.get_("tokenized-foo-2"), "foo-3;bar3,foo-4;bar4");

  const auto out = response_headers_.get(Http::LowerCaseString("tokenized-foo-3"));
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0]->value().getStringView(), "original3");
  ASSERT_EQ(out[1]->value().getStringView(), "foo-5;bar5,foo-6;bar6");
}

// Test addition of headers and tokenized headers to local reply
TEST_F(LocalReplyTest, TestHeadersAndTokenizedHeadersAddition) {
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
            key: foo
            value: bar
          append: false
      tokenized_headers_to_add:
        - name: tokenized-foo
          headers:
            - key: foo-1
              value: bar1
            - key: foo-2
              value: bar2  
          append: false   
)";
  TestUtility::loadFromYaml(yaml, config_);
  auto local = Factory::create(config_, context_);

  local->rewrite(nullptr, response_headers_, stream_info_, code_, body_, content_type_);
  EXPECT_EQ(code_, TestInitCode);
  EXPECT_EQ(stream_info_.response_code_, static_cast<uint32_t>(TestInitCode));
  EXPECT_EQ(content_type_, "text/plain");

  EXPECT_EQ(response_headers_.get_("foo"), "bar");
  EXPECT_EQ(response_headers_.get_("tokenized-foo"), "foo-1;bar1,foo-2;bar2");
}

// Test tokenized header exceeding max allowed size
TEST_F(LocalReplyTest, TestPreventTokenizedHeadersThatExceedMaxSize) {
  std::string long_key(3000, 'k');
  std::string long_value(5000, 'v');

  const std::string yaml = R"(
    mappers:
    - filter:
        status_code_filter:
          comparison:
            op: GE
            value:
              default_value: 0
              runtime_key: key_b
      tokenized_headers_to_add:
        - name: tokenized-foo
          headers:
            - key: foo-1
              value: bar1
            - key: foo-2
              value: bar2 
            - key: foo-3
              value: bar3    
          append: false   
)";
  TestUtility::loadFromYaml(yaml, config_);
  // change headers so that max size is exceeded
  for (int i = 0; i < 3; i++) {
    config_.mutable_mappers(0)->mutable_tokenized_headers_to_add(0)->mutable_headers(i)->set_key(
        long_key);
    config_.mutable_mappers(0)->mutable_tokenized_headers_to_add(0)->mutable_headers(i)->set_value(
        long_value);
  }

  EXPECT_THROW_WITH_MESSAGE(Factory::create(config_, context_), EnvoyException,
                            "exceeded max allowed size for tokenized header 'tokenized-foo'");
}

} // namespace LocalReply
} // namespace Envoy
