#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"
#include "source/extensions/filters/http/ext_proc/processing_effect.h"

#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::config::common::mutation_rules::v3::HeaderMutationRules;
using envoy::service::ext_proc::v3::BodyMutation;

using Filters::Common::MutationRules::Checker;
using Http::LowerCaseString;
using StatusHelpers::HasStatus;

class MutationUtilsTest : public ::testing::Test {
public:
  // A TestHeaderMap that adds helpers to override count and byte limits.
  class TestHeaderMapImplWithOverrides : public Http::TestRequestHeaderMapImpl {
  public:
    TestHeaderMapImplWithOverrides() = default;
    TestHeaderMapImplWithOverrides(
        std::initializer_list<std::pair<std::string, std::string>> header_list)
        : Http::TestRequestHeaderMapImpl(header_list) {}

    uint32_t maxHeadersCount() const override { return max_headers_count_; }
    void setMaxHeadersCount(uint32_t count) { max_headers_count_ = count; }

    uint32_t maxHeadersKb() const override { return max_headers_kb_; }
    void setMaxHeadersKb(uint32_t kb) { max_headers_kb_ = kb; }

  private:
    uint32_t max_headers_count_ = Http::DEFAULT_MAX_HEADERS_COUNT;
    uint32_t max_headers_kb_ = Http::DEFAULT_MAX_REQUEST_HEADERS_KB;
  };

  Regex::GoogleReEngine regex_engine_;
};

TEST_F(MutationUtilsTest, TestBuildHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };
  LowerCaseString reference_key("x-reference");
  std::string reference_value("Foo");
  headers.addReference(reference_key, reference_value);
  headers.addCopy(LowerCaseString("x-number"), 9999);

  envoy::config::core::v3::HeaderMap proto_headers;
  // Neither allow_headers nor disallow_headers is set.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);

  Http::TestRequestHeaderMapImpl expected{{":method", "GET"},
                                          {":path", "/foo/the/bar?size=123"},
                                          {"content-type", "text/plain; encoding=UTF8"},
                                          {"x-something-else", "yes"},
                                          {"x-reference", "Foo"},
                                          {"x-number", "9999"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST_F(MutationUtilsTest, TestApplyMutations) {
  Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"},
      {":authority", "localhost:1000"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-append-this", "1"},
      {"x-replace-this", "Yes"},
      {"x-remove-this", "Yes"},
      {"x-envoy-strange-thing", "No"},
  };

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_raw_value("2");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_raw_value("3");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-remove-and-append-this");
  s->mutable_header()->set_raw_value("4");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_raw_value("no");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_raw_value("418");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_raw_value("nope");
  // Incomplete structures should be ignored
  mutation.add_set_headers();

  mutation.add_remove_headers("x-remove-this");
  // remove is applied before append, so the header entry will be in the final headers.
  mutation.add_remove_headers("x-remove-and-append-this");
  // Attempts to remove ":" and "host" headers should be ignored
  mutation.add_remove_headers("host");
  mutation.add_remove_headers(":method");
  mutation.add_remove_headers("");

  // Attempts to set method, host, authority, and x-envoy headers
  // should be ignored until we explicitly allow them.
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("host");
  s->mutable_header()->set_raw_value("invalid:123");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("Host");
  s->mutable_header()->set_raw_value("invalid:456");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":authority");
  s->mutable_header()->set_raw_value("invalid:789");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":method");
  s->mutable_header()->set_raw_value("PATCH");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":scheme");
  s->mutable_header()->set_raw_value("http");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("X-Envoy-StrangeThing");
  s->mutable_header()->set_raw_value("Yes");

  // Attempts to set the status header out of range should
  // also be ignored.
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_raw_value("This is not even an integer");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_raw_value("100");

  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc()).Times(10);
  // There were 10 attempts to change un-changeable headers above.
  EXPECT_TRUE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect)
          .ok());

  Http::TestRequestHeaderMapImpl expected_headers{
      {":scheme", "https"},
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"},
      {":authority", "localhost:1000"},
      {":status", "418"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-append-this", "1"},
      {"x-append-this", "2"},
      {"x-append-this", "3"},
      {"x-remove-and-append-this", "4"},
      {"x-replace-this", "nope"},
      {"x-envoy-strange-thing", "No"},
  };

  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationApplied);
}

TEST_F(MutationUtilsTest, TestNonAppendableHeaders) {
  Http::TestRequestHeaderMapImpl headers;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":path");
  s->mutable_header()->set_raw_value("/foo");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_raw_value("400");
  // These two should be ignored since we ignore attempts
  // to set multiple values for system headers.
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":path");
  s->mutable_header()->set_raw_value("/baz");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_raw_value("401");

  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  // There were two invalid attempts above.
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc()).Times(2);
  EXPECT_TRUE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect)
          .ok());

  Http::TestRequestHeaderMapImpl expected_headers{
      {":path", "/foo"},
      {":status", "400"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationApplied);
}

TEST_F(MutationUtilsTest, TestSetHeaderWithInvalidCharacter) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {"host", "localhost:1000"},
  };
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  // Test header key contains invalid character.
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-append-this\n");
  s->mutable_header()->set_raw_value("value");
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "header_mutation_set_contains_invalid_character"));
  EXPECT_THAT(effect, ProcessingEffect::Effect::InvalidMutationRejected);

  mutation.Clear();
  s = mutation.add_set_headers();
  // Test header value contains invalid character.
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_raw_value("value\r");
  effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "header_mutation_set_contains_invalid_character"));
  EXPECT_THAT(effect, ProcessingEffect::Effect::InvalidMutationRejected);
}

TEST_F(MutationUtilsTest, TestSetHeaderWithContentLength) {
  Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"},
  };
  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  // Test header key contains content_length.
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("content-length");
  s->mutable_header()->set_raw_value("10");
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;

  EXPECT_TRUE(MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections,
                                                  effect,
                                                  /*remove_content_length=*/true)
                  .ok());
  // When `remove_content_length` is true, content_length headers is not added.
  EXPECT_EQ(headers.ContentLength(), nullptr);

  EXPECT_TRUE(MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections,
                                                  effect,
                                                  /*remove_content_length=*/false)
                  .ok());
  // When `remove_content_length` is false, content_length headers is added.
  EXPECT_EQ(headers.getContentLengthValue(), "10");
}

TEST_F(MutationUtilsTest, TestRemoveHeaderWithInvalidCharacter) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {"host", "localhost:1000"},
  };
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  mutation.add_remove_headers("host\n");
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "header_mutation_remove_contains_invalid_character"));
  EXPECT_THAT(effect, ProcessingEffect::Effect::InvalidMutationRejected);
}

// Ensure that we actually replace the body
TEST_F(MutationUtilsTest, TestBodyMutationReplace) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_body("We have replaced the value!");
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ("We have replaced the value!", buf.toString());
}

// If an empty string is included in the "body" field, we should
// replace the body with nothing
TEST_F(MutationUtilsTest, TestBodyMutationReplaceEmpty) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_body("");
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ(0, buf.length());
}

// Clear the buffer if the "clear_buffer" flag is set
TEST_F(MutationUtilsTest, TestBodyMutationClearYes) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_clear_body(true);
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ(0, buf.length());
}

// Don't clear the buffer if the "clear_buffer" flag is set to false,
// which is weird, but possible
TEST_F(MutationUtilsTest, TestBodyMutationClearNo) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  Buffer::OwnedImpl bufCopy;
  bufCopy.add(buf);
  BodyMutation mut;
  mut.set_clear_body(false);
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_TRUE(TestUtility::buffersEqual(buf, bufCopy));
}

// Nothing should happen if we don't set the proto oneof,
// which is weird, but possible
TEST_F(MutationUtilsTest, TestBodyMutationNothing) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  Buffer::OwnedImpl bufCopy;
  bufCopy.add(buf);
  BodyMutation mut;
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_TRUE(TestUtility::buffersEqual(buf, bufCopy));
}

TEST_F(MutationUtilsTest, TestAllowHeadersExactCaseSensitive) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers is set. disallow_headers is not.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;
  string_matcher.set_exact(":method");
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  string_matcher.set_exact(":Path");
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);

  Http::TestRequestHeaderMapImpl expected{{":method", "GET"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST_F(MutationUtilsTest, TestAllowHeadersExactIgnoreCase) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers is set. disallow_headers is not.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;
  string_matcher.set_exact(":method");
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  string_matcher.set_exact(":Path");
  string_matcher.set_ignore_case(true);
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{":method", "GET"}, {":path", "/foo/the/bar?size=123"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST_F(MutationUtilsTest, TestBothAllowAndDisallowHeadersSet) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::config::core::v3::HeaderMap proto_headers;
  // Both allow_headers and disallow_headers are set.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;

  // Set allow_headers.
  string_matcher.set_exact(":method");
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  string_matcher.set_exact(":path");
  allow_headers.push_back(std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));

  // Set disallow_headers
  string_matcher.set_exact(":method");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));

  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{":path", "/foo/the/bar?size=123"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST_F(MutationUtilsTest, TestDisallowHeaderSetNotAllowHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers not set. disallow_headers set.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;

  // Set disallow_headers.
  string_matcher.set_exact(":method");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));
  string_matcher.set_exact(":path");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl>(string_matcher, context));

  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{"content-type", "text/plain; encoding=UTF8"},
                                          {"x-something-else", "yes"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST_F(MutationUtilsTest, TestHeaderMutationSetOperationExceedsMaxCount) {
  TestHeaderMapImplWithOverrides headers;
  headers.setMaxHeadersCount(1);

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_header()->set_key("h5");
  s->mutable_header()->set_raw_value("v5");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("h6");
  s->mutable_header()->set_raw_value("v6");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;

  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "header_mutation_operation_count_exceeds_limit"));
  EXPECT_TRUE(headers.empty());
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, TestHeaderMutationSetResultExceedsMaxCount) {
  TestHeaderMapImplWithOverrides headers{
      {"h1", "v1"},
      {"h2", "v2"},
      {"h3", "v3"},
      {"h4", "v4"},
  };
  headers.setMaxHeadersCount(5);
  // We're now at 4 headers. One more mutation will put us at the limit,
  // and a second will put us over.

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_header()->set_key("h5");
  s->mutable_header()->set_raw_value("v5");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;

  auto status =
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect);
  EXPECT_TRUE(status.ok());

  s = mutation.add_set_headers();
  s->mutable_header()->set_key("h6");
  s->mutable_header()->set_raw_value("v6");
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_result_exceeds_limit"));
  // Surprise: While we return an error, the headers actually DO get mutated.
  // (Filter must detect the error status and discard the mutation.)
  EXPECT_EQ(headers.size(), 6);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, TestHeaderMutationRemoveOperationExceedsMaxCount) {
  TestHeaderMapImplWithOverrides headers{
      {"h1", "v1"},
      {"h2", "v2"},
  };
  headers.setMaxHeadersCount(1);

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  mutation.add_remove_headers("h1");
  mutation.add_remove_headers("h2");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument,
                "header_mutation_operation_count_exceeds_limit"));
  EXPECT_EQ(headers.size(), 2);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, TestHeaderMutationExceedsMaxKb) {
  TestHeaderMapImplWithOverrides headers;
  headers.setMaxHeadersKb(1);
  // Fill up the headers part of the way to the 1kb limit
  headers.addCopy(LowerCaseString("header1"), std::string(1002, 'a'));
  ASSERT_EQ(headers.byteSize(), 1009);

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  // This next header should bring us almost to the limit.
  s->mutable_header()->set_key("header2");
  s->mutable_header()->set_raw_value("b");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;

  auto status =
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect);
  EXPECT_TRUE(status.ok());
  ASSERT_EQ(headers.byteSize(), 1017);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationApplied);

  // This last header should push us over the limit.
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("header3");
  s->mutable_header()->set_raw_value("c");
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_result_exceeds_limit"));
  // Surprise: While we return an error, the headers actually DO get mutated.
  // (Filter must detect the error status and discard the mutation.)
  EXPECT_EQ(headers.byteSize(), 1025);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, TestHeaderMutationRemoveResultExceedsMaxCount) {
  TestHeaderMapImplWithOverrides headers{
      {"h1", "v1"},
      {"h2", "v2"},
      {"h3", "v3"},
  };
  headers.setMaxHeadersCount(1);

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  mutation.add_remove_headers("h3");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_result_exceeds_limit"));
  // Surprise: h3 was removed despite the error!
  // (Filter must detect the error status and discard the mutation.)
  EXPECT_EQ(headers.size(), 2);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, TestHeaderMutationExceedsMaxCountAndSize) {
  TestHeaderMapImplWithOverrides headers{
      {"h0", "v0"},
  };
  headers.setMaxHeadersCount(1);
  headers.setMaxHeadersKb(1);
  // Fill up the headers to the 1kb limit
  headers.addCopy(LowerCaseString("h1"), std::string(1018, 'v'));
  ASSERT_EQ(headers.byteSize(), 1024);

  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_header()->set_key("h0");
  s->mutable_header()->set_raw_value("v00");

  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  ProcessingEffect::Effect effect = ProcessingEffect::Effect::None;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections, effect),
      HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_result_exceeds_limit"));
  EXPECT_EQ(headers.size(), 2);
  EXPECT_THAT(effect, ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

TEST_F(MutationUtilsTest, ProtoToHeaders) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "200"
    }
    headers {
      key: "some-header"
      raw_value: "value"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  Http::TestResponseHeaderMapImpl headers;
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_OK(MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections));
  Http::TestResponseHeaderMapImpl expected{{":status", "200"}, {"some-header", "value"}};
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST_F(MutationUtilsTest, ProtoToHeadersTooManyHeaders) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "200"
    }
    headers {
      key: "some-header"
      raw_value: "value"
    }
    headers {
      key: "some-header-2"
      raw_value: "value2"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  TestHeaderMapImplWithOverrides headers;
  headers.setMaxHeadersCount(2);
  headers.setMaxHeadersKb(1);
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "header_mutation_operation_count_exceeds_limit"));
}

TEST_F(MutationUtilsTest, ProtoToHeadersTooLargeHeader) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "200"
    }
    headers {
      key: "some-header"
      raw_value: "value"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  headers_proto.mutable_headers(1)->set_raw_value(std::string(2048, 'v'));
  TestHeaderMapImplWithOverrides headers;
  headers.setMaxHeadersCount(3);
  // limit is lower than 2Kb header in the proto
  headers.setMaxHeadersKb(1);
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(
      MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections),
      HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_result_exceeds_limit"));
}

TEST_F(MutationUtilsTest, ProtoToHeadersXEnvoyDisallowed) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "200"
    }
    headers {
      key: "x-envoy-some-header"
      raw_value: "value"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  Http::TestResponseHeaderMapImpl headers;
  // By default x-envoy headers are disallowed.
  Checker checker(HeaderMutationRules::default_instance(), regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc());
  EXPECT_OK(MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections));
  // x-envoy header is dropped and the rejections counter is incremented.
  Http::TestResponseHeaderMapImpl expected{{":status", "200"}};
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST_F(MutationUtilsTest, StatusIsPreservedEvenWhenDisallowed) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "200"
    }
    headers {
      key: "x-some-header"
      raw_value: "value"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  Http::TestResponseHeaderMapImpl headers;
  HeaderMutationRules rules;
  rules.mutable_disallow_system()->set_value(true);
  Checker checker(rules, regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc()).Times(0);
  EXPECT_OK(MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections));
  Http::TestResponseHeaderMapImpl expected{{":status", "200"}, {"x-some-header", "value"}};
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected));
}

TEST_F(MutationUtilsTest, InvalidStatusRejected) {
  constexpr absl::string_view header_map = R"pb(
    headers {
      key: ":status"
      raw_value: "foobar"
    }
    headers {
      key: "x-some-header"
      raw_value: "value"
    }
  )pb";
  envoy::config::core::v3::HeaderMap headers_proto;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(header_map, &headers_proto));
  Http::TestResponseHeaderMapImpl headers;
  HeaderMutationRules rules;
  rules.mutable_disallow_system()->set_value(true);
  Checker checker(rules, regex_engine_);
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc());
  EXPECT_THAT(MutationUtils::protoToHeaders(headers_proto, headers, checker, rejections),
              HasStatus(absl::StatusCode::kInvalidArgument, "header_mutation_set_headers_failed"));
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
