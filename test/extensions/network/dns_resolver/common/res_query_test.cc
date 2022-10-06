#include "source/extensions/network/dns_resolver/common/res_query.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

TEST(SrvTest, ResolveGoogle) {
  auto result = doSrvLookup("google.com", 443);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->resolved_addresses.size() > 0);
  ASSERT_EQ(result->resolved_addresses[0]->ip()->port(), 443);
}

TEST(SrvTest, ResolveFail) {
  auto result = doSrvLookup("subdomainnotfound.example.com", 443);
  ASSERT_FALSE(result.ok());
}

} // namespace
} // namespace Network
} // namespace Envoy
