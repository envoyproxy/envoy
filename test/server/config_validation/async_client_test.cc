#include "envoy/http/message.h"

#include "common/http/message_impl.h"

#include "server/config_validation/async_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Http {

TEST(ValidationAsyncClientTest, MockedMethods) {
  MessagePtr message{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks;
  MockAsyncClientStreamCallbacks stream_callbacks;

  Api::ApiPtr api = Api::createApiForTest();
  ValidationAsyncClient client(*api);
  EXPECT_EQ(nullptr, client.send(std::move(message), callbacks, AsyncClient::RequestOptions()));
  EXPECT_EQ(nullptr, client.start(stream_callbacks, AsyncClient::StreamOptions()));
}

} // namespace Http
} // namespace Envoy
