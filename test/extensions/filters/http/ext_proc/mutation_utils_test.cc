#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::service::ext_proc::v3alpha::BodyMutation;

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
  MutationUtils::buildHttpHeaders(headers, proto_headers);

  Http::TestRequestHeaderMapImpl expected{{":method", "GET"},
                                          {":path", "/foo/the/bar?size=123"},
                                          {"content-type", "text/plain; encoding=UTF8"},
                                          {"x-something-else", "yes"},
                                          {"x-reference", "Foo"},
                                          {"x-number", "9999"}};
  EXPECT_THAT(proto_headers, HeaderProtosEqual(expected));
}

TEST(MutationUtils, TestApplyMutations) {
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

  envoy::service::ext_proc::v3alpha::HeaderMutation mutation;
  auto* s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_value("2");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(true);
  s->mutable_header()->set_key("x-append-this");
  s->mutable_header()->set_value("3");
  s = mutation.add_set_headers();
  s->mutable_append()->set_value(false);
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_value("no");
  // Default of "append" is "false" and mutations
  // are applied in order.
  s = mutation.add_set_headers();
  s->mutable_header()->set_key("x-replace-this");
  s->mutable_header()->set_value("nope");
  // Incomplete structures should be ignored
  mutation.add_set_headers();

  mutation.add_remove_headers("x-remove-this");
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

  MutationUtils::applyHeaderMutations(mutation, headers);

  Http::TestRequestHeaderMapImpl expected_headers{
      {":scheme", "https"},
      {":method", "GET"},
      {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"},
      {":authority", "localhost:1000"},
      {"content-type", "text/plain; encoding=UTF8"},
      {"x-append-this", "1"},
      {"x-append-this", "2"},
      {"x-append-this", "3"},
      {"x-replace-this", "nope"},
      {"x-envoy-strange-thing", "No"},
  };

  EXPECT_THAT(&headers, HeaderMapEqualIgnoreOrder(&expected_headers));
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

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy