#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/early_header_mutation/exact_path_rewrite/header_mutation.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {
namespace {

ProtoExactPathRewrite makeConfig(const std::string& yaml) {
  ProtoExactPathRewrite config;
  TestUtility::loadFromYaml(yaml, config);
  return config;
}

Envoy::Http::TestRequestHeaderMapImpl makeHeaders(absl::string_view authority,
                                                  absl::string_view path) {
  return {{":method", "GET"},
          {":path", std::string(path)},
          {":scheme", "http"},
          {":authority", std::string(authority)}};
}

TEST(ExactPathRewriteTest, RewritesOnlyExactPathAndPreservesQuery) {
  const auto config = makeConfig(R"EOF(
host_header: ":authority"
hosts:
- domains: ["api.example.com"]
  rules:
  - exact_path: /api
    replacement_path: /api/
)EOF");
  auto mutation = ExactPathRewrite::create(config).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  auto headers = makeHeaders("api.example.com", "/api?one=1?two=2");
  EXPECT_TRUE(mutation->mutate(headers, stream_info));
  EXPECT_EQ("/api/?one=1?two=2", headers.getPathValue());

  for (const absl::string_view path :
       {"/api/", "/api/child", "/api2", "/API", "/api;parameter", "/api%2F"}) {
    auto unchanged = makeHeaders("api.example.com", path);
    EXPECT_TRUE(mutation->mutate(unchanged, stream_info));
    EXPECT_EQ(path, unchanged.getPathValue());
  }
}

TEST(ExactPathRewriteTest, SelectsMostSpecificHostRuleSet) {
  const auto config = makeConfig(R"EOF(
host_header: ":authority"
hosts:
- domains: ["*.example.com"]
  rules:
  - exact_path: /api
    replacement_path: /api/
- domains: ["admin.example.com"]
  rules: []
- domains: ["*"]
  rules:
  - exact_path: /v1
    replacement_path: /v1/
)EOF");
  auto mutation = ExactPathRewrite::create(config).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  auto wildcard = makeHeaders("shop.example.com", "/api");
  mutation->mutate(wildcard, stream_info);
  EXPECT_EQ("/api/", wildcard.getPathValue());

  auto empty_rules = makeHeaders("admin.example.com", "/api");
  mutation->mutate(empty_rules, stream_info);
  EXPECT_EQ("/api", empty_rules.getPathValue());

  auto default_host = makeHeaders("other.example.net", "/v1?");
  mutation->mutate(default_host, stream_info);
  EXPECT_EQ("/v1/?", default_host.getPathValue());
}

TEST(ExactPathRewriteTest, RejectsInvalidConfiguration) {
  const auto duplicate_path = makeConfig(R"EOF(
host_header: ":authority"
hosts:
- domains: ["api.example.com"]
  rules:
  - exact_path: /api
    replacement_path: /api/
  - exact_path: /api
    replacement_path: /other/
)EOF");
  EXPECT_EQ(ExactPathRewrite::create(duplicate_path).status().code(),
            absl::StatusCode::kInvalidArgument);

  const auto query_path = makeConfig(R"EOF(
host_header: ":authority"
hosts:
- domains: ["api.example.com"]
  rules:
  - exact_path: /api?query
    replacement_path: /api/
)EOF");
  EXPECT_EQ(ExactPathRewrite::create(query_path).status().code(),
            absl::StatusCode::kInvalidArgument);
}

} // namespace
} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
