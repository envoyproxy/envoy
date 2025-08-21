#include "source/common/http/header_mutation.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

TEST(HeaderMutationsTest, BasicRemove) {
  ProtoHeaderMutatons proto_mutations;
  proto_mutations.Add()->set_remove("flag-header");
  proto_mutations.Add()->set_remove("another-flag-header");

  auto mutations = HeaderMutations::create(proto_mutations).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"not-flag-header", "not-flag-header-value"},
        {":method", "GET"},
        {":path", "/"},
        {":authority", "host"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);
    EXPECT_EQ("", headers.get_("flag-header"));
    EXPECT_EQ("", headers.get_("another-flag-header"));
    EXPECT_EQ("not-flag-header-value", headers.get_("not-flag-header"));
  }
}

TEST(HeaderMutationsTest, AllOperations) {
  ProtoHeaderMutatons proto_mutations;
  // Step 1: Remove 'flag-header' header.
  proto_mutations.Add()->set_remove("flag-header");

  // Step 2: Append 'flag-header' header.
  auto append = proto_mutations.Add()->mutable_append();
  append->mutable_header()->set_key("flag-header");
  append->mutable_header()->set_value("%REQ(ANOTHER-FLAG-HEADER)%");
  append->set_append_action(ProtoHeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  // Step 3: Append 'flag-header-2' header.
  auto append2 = proto_mutations.Add()->mutable_append();
  append2->mutable_header()->set_key("flag-header-2");
  append2->mutable_header()->set_value("flag-header-2-value");
  append2->set_append_action(ProtoHeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

  // Step 4: Append 'flag-header-3' header if not exist.
  auto append3 = proto_mutations.Add()->mutable_append();
  append3->mutable_header()->set_key("flag-header-3");
  append3->mutable_header()->set_value("flag-header-3-value");
  append3->set_append_action(ProtoHeaderValueOption::ADD_IF_ABSENT);

  // Step 4: Overwrite 'flag-header-4' header if exist.
  auto append4 = proto_mutations.Add()->mutable_append();
  append4->mutable_header()->set_key("flag-header-4");
  append4->mutable_header()->set_value("flag-header-4-value");
  append4->set_append_action(ProtoHeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

  auto mutations = HeaderMutations::create(proto_mutations).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Remove 'flag-header' and try to append 'flag-header' with value 'another-flag-header-value'.
  // But 'another-flag-header' is not found, so 'flag-header' is not appended.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    // Original 'flag-header' is removed and no new value is appended because there is no
    // 'another-flag-header'.
    EXPECT_EQ(0, headers.get(Http::LowerCaseString("flag-header")).size());
  }
  // Remove 'flag-header' and try to append 'flag-header' with value 'another-flag-header-value'.
  // 'another-flag-header' is found, so 'flag-header' is appended.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    // Original 'flag-header' is removed and the new value is appended.
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
  }

  // Simple append 'flag-header-2' and a old value is exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header-2", "flag-header-2-value-old"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(2, headers.get(Http::LowerCaseString("flag-header-2")).size());
  }
  // Simple append 'flag-header-2' and a old value is not exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-2")).size());
  }

  // Add header 'flag-header-3' if it is not exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-3")).size());
  }
  // Skip add header 'flag-header-3' if it is exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header-3", "flag-header-3-value-old"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-3")).size());
    EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
  }

  // Overwrite header 'flag-header-4' if it is exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header-4", "flag-header-4-value-old"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
  }
  // Add header 'flag-header-4' if it is not exist.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
  }

  // Hybrid case.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {"flag-header-2", "flag-header-2-value-old"},
        {"flag-header-3", "flag-header-3-value-old"},
        {"flag-header-4", "flag-header-4-value-old"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    // 'flag-header' is removed and new 'flag-header' is added.
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
    // 'flag-header-2' is appended.
    EXPECT_EQ(2, headers.get(Http::LowerCaseString("flag-header-2")).size());
    // 'flag-header-3' is not appended and keep the old value.
    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-3")).size());
    EXPECT_EQ("flag-header-3-value-old", headers.get_("flag-header-3"));
    // 'flag-header-4' is overwritten.
    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header-4")).size());
    EXPECT_EQ("flag-header-4-value", headers.get_("flag-header-4"));
  }
}

TEST(HeaderMutationsTest, KeepEmptyValue) {
  ProtoHeaderMutatons proto_mutations;
  // Step 1: Remove the header.
  proto_mutations.Add()->set_remove("flag-header");

  // Step 2: Append the header and keep empty value if the source header is not found.
  auto append = proto_mutations.Add()->mutable_append();
  append->mutable_header()->set_key("flag-header");
  append->mutable_header()->set_value("%REQ(ANOTHER-FLAG-HEADER)%");
  append->set_append_action(ProtoHeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
  append->set_keep_empty_value(true);

  auto mutations = HeaderMutations::create(proto_mutations).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);

    // Original 'flag-header' is removed and empty value is appended.
    EXPECT_EQ(2, headers.size());
    EXPECT_EQ(1, headers.get(Http::LowerCaseString("flag-header")).size());
    EXPECT_EQ("", headers.get_("flag-header"));
  }
}

TEST(HeaderMutationsTest, BasicOrder) {
  {
    ProtoHeaderMutatons proto_mutations;

    // Step 1: Append the header.
    auto append = proto_mutations.Add()->mutable_append();
    append->mutable_header()->set_key("flag-header");
    append->mutable_header()->set_value("%REQ(ANOTHER-FLAG-HEADER)%");
    append->set_append_action(ProtoHeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

    // Step 2: Remove the header.
    proto_mutations.Add()->set_remove("flag-header");

    auto mutations = HeaderMutations::create(proto_mutations).value();
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);
    EXPECT_EQ("", headers.get_("flag-header"));
    EXPECT_EQ(0, headers.get(Http::LowerCaseString("flag-header")).size());
  }

  {
    ProtoHeaderMutatons proto_mutations;
    // Step 1: Remove the header.
    proto_mutations.Add()->set_remove("flag-header");

    // Step 2: Append the header.
    auto append = proto_mutations.Add()->mutable_append();
    append->mutable_header()->set_key("flag-header");
    append->mutable_header()->set_value("%REQ(ANOTHER-FLAG-HEADER)%");
    append->set_append_action(ProtoHeaderValueOption::APPEND_IF_EXISTS_OR_ADD);

    auto mutations = HeaderMutations::create(proto_mutations).value();
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

    Envoy::Http::TestRequestHeaderMapImpl headers = {
        {"flag-header", "flag-header-value"},
        {"another-flag-header", "another-flag-header-value"},
        {":method", "GET"},
    };

    mutations->evaluateHeaders(headers, {&headers}, stream_info);
    EXPECT_EQ("another-flag-header-value", headers.get_("flag-header"));
  }
}

TEST(HeaderMutationTest, Death) {
  ProtoHeaderMutatons proto_mutations;
  proto_mutations.Add();

  EXPECT_DEATH(HeaderMutations::create(proto_mutations).IgnoreError(), "unset oneof");
}

} // namespace
} // namespace Http
} // namespace Envoy
