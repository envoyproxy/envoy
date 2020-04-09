#include "envoy/http/message.h"

#include "common/http/message_impl.h"

#include "server/config_validation/async_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Http {
namespace {

TEST(ValidationAsyncClientTest, MockedMethods) {
  RequestMessagePtr message{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks;
  MockAsyncClientStreamCallbacks stream_callbacks;

  Event::SimulatedTimeSystem time_system;
  Api::ApiPtr api = Api::createApiForTest(time_system);
  ValidationAsyncClient client(*api, time_system);
  EXPECT_EQ(nullptr, client.send(std::move(message), callbacks, AsyncClient::RequestOptions()));
  EXPECT_EQ(nullptr, client.start(stream_callbacks, AsyncClient::StreamOptions()));
}

} // namespace
} // namespace Http
} // namespace Envoy
