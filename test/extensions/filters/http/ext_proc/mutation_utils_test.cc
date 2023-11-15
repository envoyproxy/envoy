#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/test_runtime.h"
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

TEST(MutationUtils, TestBuildHeaders) {
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

TEST(MutationUtils, TestApplyMutations) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.send_header_raw_value", "false"}});
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
  s->mutable_header()->set_value("2");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_value("3");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-remove-and-append-this");
  s->mutable_header()->set_value("4");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_value("no");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_value("418");
  // Default of "append" is "false" and mutations
  // are applied in order.
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_value("nope");
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
  s->mutable_header()->set_key("host");
  s->mutable_header()->set_value("invalid:123");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("Host");
  s->mutable_header()->set_value("invalid:456");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":authority");
  s->mutable_header()->set_value("invalid:789");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":method");
  s->mutable_header()->set_value("PATCH");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":scheme");
  s->mutable_header()->set_value("http");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("X-Envoy-StrangeThing");
  s->mutable_header()->set_value("Yes");

  // Attempts to set the status header out of range should
  // also be ignored.
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_value("This is not even an integer");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_value("100");

  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance());
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc()).Times(10);
  // There were 10 attempts to change un-changeable headers above.
  EXPECT_TRUE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections).ok());

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
}

TEST(MutationUtils, TestNonAppendableHeaders) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.send_header_raw_value", "false"}});
  Http::TestRequestHeaderMapImpl headers;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":path");
  s->mutable_header()->set_value("/foo");
  s = mutation.add_set_headers();
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_value("400");
  // These two should be ignored since we ignore attempts
  // to set multiple values for system headers.
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":path");
  s->mutable_header()->set_value("/baz");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key(":status");
  s->mutable_header()->set_value("401");

  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance());
  // There were two invalid attempts above.
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc()).Times(2);
  EXPECT_TRUE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections).ok());

  Http::TestRequestHeaderMapImpl expected_headers{
      {":path", "/foo"},
      {":status", "400"},
  };
  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
}

TEST(MutationUtils, TestSetHeaderWithInvalidCharacter) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.send_header_raw_value", "false"}});
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {"host", "localhost:1000"},
  };
  Checker checker(HeaderMutationRules::default_instance());
  Envoy::Stats::MockCounter rejections;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  // Test header key contains invalid character.
  s->mutable_header()->set_key("x-append-this\n");
  s->mutable_header()->set_value("value");
  EXPECT_CALL(rejections, inc());
  EXPECT_FALSE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections).ok());

  mutation.Clear();
  s = mutation.add_set_headers();
  // Test header value contains invalid character.
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_value("value\r");
  EXPECT_CALL(rejections, inc());
  EXPECT_FALSE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections).ok());
}

TEST(MutationUtils, TestSetHeaderWithContentLength) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.send_header_raw_value", "false"}});
  Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"},
  };
  // Use the default mutation rules
  Checker checker(HeaderMutationRules::default_instance());
  Envoy::Stats::MockCounter rejections;
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  // Test header key contains content_length.
  s->mutable_header()->set_key("content-length");
  s->mutable_header()->set_value("10");

  EXPECT_TRUE(MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections,
                                                  /*remove_content_length=*/true)
                  .ok());
  // When `remove_content_length` is true, content_length headers is not added.
  EXPECT_EQ(headers.ContentLength(), nullptr);

  EXPECT_TRUE(MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections,
                                                  /*remove_content_length=*/false)
                  .ok());
  // When `remove_content_length` is false, content_length headers is added.
  EXPECT_EQ(headers.getContentLengthValue(), "10");
}

TEST(MutationUtils, TestRemoveHeaderWithInvalidCharacter) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {"host", "localhost:1000"},
  };
  envoy::service::ext_proc::v3::HeaderMutation mutation;
  mutation.add_remove_headers("host\n");
  Checker checker(HeaderMutationRules::default_instance());
  Envoy::Stats::MockCounter rejections;
  EXPECT_CALL(rejections, inc());
  EXPECT_FALSE(
      MutationUtils::applyHeaderMutations(mutation, headers, false, checker, rejections).ok());
}

// Ensure that we actually replace the body
TEST(MutationUtils, TestBodyMutationReplace) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_body("We have replaced the value!");
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ("We have replaced the value!", buf.toString());
}

// If an empty string is included in the "body" field, we should
// replace the body with nothing
TEST(MutationUtils, TestBodyMutationReplaceEmpty) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_body("");
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ(0, buf.length());
}

// Clear the buffer if the "clear_buffer" flag is set
TEST(MutationUtils, TestBodyMutationClearYes) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  BodyMutation mut;
  mut.set_clear_body(true);
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_EQ(0, buf.length());
}

// Don't clear the buffer if the "clear_buffer" flag is set to false,
// which is weird, but possible
TEST(MutationUtils, TestBodyMutationClearNo) {
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
TEST(MutationUtils, TestBodyMutationNothing) {
  Buffer::OwnedImpl buf;
  TestUtility::feedBufferWithRandomCharacters(buf, 100);
  Buffer::OwnedImpl bufCopy;
  bufCopy.add(buf);
  BodyMutation mut;
  MutationUtils::applyBodyMutations(mut, buf);
  EXPECT_TRUE(TestUtility::buffersEqual(buf, bufCopy));
}

TEST(MutationUtils, TestAllowHeadersExactCaseSensitive) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers is set. disallow_headers is not.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;
  string_matcher.set_exact(":method");
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  string_matcher.set_exact(":Path");
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);

  Http::TestRequestHeaderMapImpl expected{{":method", "GET"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST(MutationUtils, TestAllowHeadersExactIgnoreCase) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };
  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers is set. disallow_headers is not.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;
  string_matcher.set_exact(":method");
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  string_matcher.set_exact(":Path");
  string_matcher.set_ignore_case(true);
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{":method", "GET"}, {":path", "/foo/the/bar?size=123"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST(MutationUtils, TestBothAllowAndDisallowHeadersSet) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  envoy::config::core::v3::HeaderMap proto_headers;
  // Both allow_headers and disallow_headers are set.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;

  // Set allow_headers.
  string_matcher.set_exact(":method");
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  string_matcher.set_exact(":path");
  allow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));

  // Set disallow_headers
  string_matcher.set_exact(":method");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));

  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{":path", "/foo/the/bar?size=123"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST(MutationUtils, TestDisallowHeaderSetNotAllowHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-something-else", "yes"},
  };

  envoy::config::core::v3::HeaderMap proto_headers;
  // allow_headers not set. disallow_headers set.
  std::vector<Matchers::StringMatcherPtr> allow_headers;
  std::vector<Matchers::StringMatcherPtr> disallow_headers;
  envoy::type::matcher::v3::StringMatcher string_matcher;

  // Set disallow_headers.
  string_matcher.set_exact(":method");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));
  string_matcher.set_exact(":path");
  disallow_headers.push_back(
      std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
          string_matcher));

  MutationUtils::headersToProto(headers, allow_headers, disallow_headers, proto_headers);
  Http::TestRequestHeaderMapImpl expected{{"content-type", "text/plain; encoding=UTF8"},
                                          {"x-something-else", "yes"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
