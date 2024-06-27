#include <string>
#include <vector>

#include "examples/cc/fetch_client/fetch_client.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Platform {
namespace {

// This test verifies that the fetch client is able to successfully
// build and start the Envoy engine. It will panic if it is unable
// to do so.
TEST(FetchClientTest, Http2) {
  Envoy::Fetch client;
  ASSERT_EQ(client.fetch({"https://www.google.com/"}), ENVOY_SUCCESS);
}

TEST(FetchClientTest, Http3) {
  Envoy::Fetch client;
  ASSERT_EQ(client.fetch({"https://www.google.com/"}, {"www.google.com"}), ENVOY_SUCCESS);
}

} // namespace
} // namespace Platform
} // namespace Envoy
