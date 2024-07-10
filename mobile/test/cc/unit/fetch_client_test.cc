#include <string>
#include <vector>

#include "examples/cc/fetch_client/fetch_client.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Platform {
namespace {

TEST(FetchClientTest, Http2) {
  Envoy::Fetch client;
  std::vector<Http::Protocol> protocols;
  ASSERT_EQ(client.fetch({"https://www.google.com/"}, {}, protocols), ENVOY_SUCCESS);
  ASSERT_EQ(protocols.front(), Http::Protocol::Http2);
}

TEST(FetchClientTest, Http3) {
  Envoy::Fetch client;
  std::vector<Http::Protocol> protocols;
  ASSERT_EQ(client.fetch({"https://www.google.com/"}, {"www.google.com"}, protocols),
            ENVOY_SUCCESS);
  // TODO(fredyw): In CI, HTTP/3 does not work and will use HTTP/2 instead.
  ASSERT_GT(protocols.front(), Http::Protocol::Http11);
}

} // namespace
} // namespace Platform
} // namespace Envoy
