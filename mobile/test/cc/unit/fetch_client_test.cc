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
  ASSERT_EQ(client.fetch({"https://www.google.com/", "https://www.google.com/"}, {"www.google.com"},
                         protocols),
            ENVOY_SUCCESS);
  // The first request could either be HTTP/2 or HTTP/3 because we no longer give HTTP/3 a head
  // start.
  ASSERT_GE(protocols.at(0), Http::Protocol::Http2);
  // TODO(fredyw): In EngFlow CI, HTTP/3 does not work and will use HTTP/2 instead.
  ASSERT_GE(protocols.at(1), Http::Protocol::Http3);
}

} // namespace
} // namespace Platform
} // namespace Envoy
