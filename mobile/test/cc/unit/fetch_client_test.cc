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
TEST(FetchClientTest, Foo) {
  Envoy::Fetch client;
  client.fetch({"https://www.google.com/"});
}

} // namespace
} // namespace Platform
} // namespace Envoy
