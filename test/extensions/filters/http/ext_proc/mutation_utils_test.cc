#include "extensions/filters/http/ext_proc/mutation_utils.h"

#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

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

  for (const auto& it : proto_headers.headers()) {
    std::cerr << it.key() << ": " << it.value() << '\n';
  }

  expectHttpHeader(proto_headers, ":method", "GET");
  expectHttpHeader(proto_headers, ":path", "/foo/the/bar?size=123");
  expectHttpHeader(proto_headers, "content-type", "text/plain; encoding=UTF8");
  expectHttpHeader(proto_headers, "x-something-else", "yes");
  expectHttpHeader(proto_headers, "x-reference", "Foo");
  expectHttpHeader(proto_headers, "x-number", "9999");
}

TEST(MutationUtils, TestApplyMutations) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},         {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"}, {"content-type", "text/plain; encoding=UTF8"},
      {"x-append-this", "1"},     {"x-replace-this", "Yes"},
      {"x-remove-this", "Yes"},
  };

  envoy::service::ext_proc::v3alpha::HeaderMutation mutation;
  auto* set1 = mutation.add_set_headers();
  set1->mutable_append()->set_value(true);
  set1->mutable_header()->set_key("x-append-this");
  set1->mutable_header()->set_value("2");
  auto* set2 = mutation.add_set_headers();
  set2->mutable_append()->set_value(true);
  set2->mutable_header()->set_key("x-append-this");
  set2->mutable_header()->set_value("3");
  auto* set3 = mutation.add_set_headers();
  set3->mutable_append()->set_value(false);
  set3->mutable_header()->set_key("x-replace-this");
  set3->mutable_header()->set_value("no");
  // Default of "append" is "false" and mutations
  // are applied in order.
  auto* set4 = mutation.add_set_headers();
  set4->mutable_header()->set_key("x-replace-this");
  set4->mutable_header()->set_value("nope");
  // Incomplete structures should be ignored
  mutation.add_set_headers();

  mutation.add_remove_headers("x-remove-this");
  // Attempts to remove ":" and "host" headers should be ignored
  mutation.add_remove_headers("host");
  mutation.add_remove_headers(":method");
  mutation.add_remove_headers("");

  MutationUtils::applyHeaderMutations(mutation, headers);

  Http::TestRequestHeaderMapImpl expected_headers{
      {":method", "GET"},         {":path", "/foo/the/bar?size=123"},
      {"host", "localhost:1000"}, {"content-type", "text/plain; encoding=UTF8"},
      {"x-append-this", "1"},     {"x-append-this", "2"},
      {"x-append-this", "3"},     {"x-replace-this", "nope"},
  };

  ASSERT_TRUE(TestUtility::headerMapEqualIgnoreOrder(headers, expected_headers));
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy