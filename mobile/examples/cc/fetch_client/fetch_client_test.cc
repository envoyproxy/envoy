#include <string>
#include <vector>

#include "testing/base/public/gunit.h"
#include "third_party/envoy/src/mobile/examples/cc/fetch_client/fetch_client.h"

namespace Envoy {
namespace Platform {
namespace {

TEST(FetchClientTest, Foo) {
  Envoy::Fetch client;
  client.fetch({"https://www.google.com/"});
}

} // namespace
} // namespace Platform
} // namespace Envoy
