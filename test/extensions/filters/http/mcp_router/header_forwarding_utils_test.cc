#include "envoy/extensions/filters/http/mcp_router/v3/mcp_router.pb.h"

#include "source/extensions/filters/http/mcp_router/header_forwarding_utils.h"

#include "test/mocks/server/server_factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {
namespace {

using McpBackendProto = envoy::extensions::filters::http::mcp_router::v3::McpRouter_McpBackend;

class HeaderForwardingUtilsTest : public ::testing::Test {
protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;

  static Matchers::StringMatcherPtr
  exactMatcher(absl::string_view value, bool ignore_case,
               Server::Configuration::CommonFactoryContext& context) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_exact(std::string(value));
    matcher.set_ignore_case(ignore_case);
    return std::make_unique<Matchers::StringMatcherImpl>(matcher, context);
  }

  static Matchers::StringMatcherPtr
  prefixMatcher(absl::string_view value, Server::Configuration::CommonFactoryContext& context) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_prefix(std::string(value));
    return std::make_unique<Matchers::StringMatcherImpl>(matcher, context);
  }
};

// Secure-by-default: with no forward_all, no allowed_headers, no disallowed_headers, nothing
// is forwarded. This is the core behavioral requirement of issue #46525 -- unlike ext_proc's
// HeaderForwardingRules (empty = forward everything), an empty MCP policy forwards nothing.
TEST_F(HeaderForwardingUtilsTest, EmptyPolicyForwardsNothing) {
  HeaderForwardingPolicy policy;
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
  EXPECT_FALSE(headerCanBeForwarded("x-anything", policy));
}

TEST_F(HeaderForwardingUtilsTest, ForwardAllForwardsEverythingByDefault) {
  HeaderForwardingPolicy policy;
  policy.forward_all = true;
  EXPECT_TRUE(headerCanBeForwarded("authorization", policy));
  EXPECT_TRUE(headerCanBeForwarded("x-anything", policy));
}

TEST_F(HeaderForwardingUtilsTest, DisallowedHeaderWinsOverForwardAll) {
  HeaderForwardingPolicy policy;
  policy.forward_all = true;
  policy.disallowed_headers.push_back(exactMatcher("authorization", false, context_));
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
  EXPECT_TRUE(headerCanBeForwarded("x-anything", policy));
}

TEST_F(HeaderForwardingUtilsTest, AllowedHeaderIsForwardedWhenNotForwardingAll) {
  HeaderForwardingPolicy policy;
  policy.allowed_headers.push_back(exactMatcher("x-trace-id", false, context_));
  EXPECT_TRUE(headerCanBeForwarded("x-trace-id", policy));
  // Not in the allow list, and forward_all is false -- must not be forwarded.
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
}

TEST_F(HeaderForwardingUtilsTest, DisallowedHeaderWinsOverAllowedHeader) {
  HeaderForwardingPolicy policy;
  policy.allowed_headers.push_back(exactMatcher("authorization", false, context_));
  policy.disallowed_headers.push_back(exactMatcher("authorization", false, context_));
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
}

TEST_F(HeaderForwardingUtilsTest, AllowedHeaderSupportsPrefixMatching) {
  HeaderForwardingPolicy policy;
  policy.allowed_headers.push_back(prefixMatcher("x-trace-", context_));
  EXPECT_TRUE(headerCanBeForwarded("x-trace-id", policy));
  EXPECT_TRUE(headerCanBeForwarded("x-trace-parent", policy));
  EXPECT_FALSE(headerCanBeForwarded("x-other", policy));
}

TEST_F(HeaderForwardingUtilsTest, AllowedHeaderIgnoreCase) {
  HeaderForwardingPolicy policy;
  policy.allowed_headers.push_back(exactMatcher("X-Trace-Id", true, context_));
  EXPECT_TRUE(headerCanBeForwarded("x-trace-id", policy));
}

TEST_F(HeaderForwardingUtilsTest, ParseHeaderForwardingDefaultsToSecurePolicy) {
  McpBackendProto backend;
  HeaderForwardingPolicy policy = parseHeaderForwarding(backend.header_forwarding(), context_);
  EXPECT_FALSE(policy.forward_all);
  EXPECT_TRUE(policy.allowed_headers.empty());
  EXPECT_TRUE(policy.disallowed_headers.empty());
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
}

TEST_F(HeaderForwardingUtilsTest, ParseHeaderForwardingParsesForwardAllAndMatchers) {
  McpBackendProto backend;
  auto* header_forwarding = backend.mutable_header_forwarding();
  header_forwarding->set_forward_all(true);
  header_forwarding->mutable_disallowed_headers()->add_patterns()->set_exact("authorization");

  HeaderForwardingPolicy policy = parseHeaderForwarding(backend.header_forwarding(), context_);
  EXPECT_TRUE(policy.forward_all);
  EXPECT_EQ(1, policy.disallowed_headers.size());
  EXPECT_TRUE(headerCanBeForwarded("x-anything", policy));
  EXPECT_FALSE(headerCanBeForwarded("authorization", policy));
}

} // namespace
} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
