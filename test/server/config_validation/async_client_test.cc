#include "envoy/http/message.h"

#include "common/http/message_impl.h"

#include "server/config_validation/async_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
using testing::StrictMock;
namespace Http {

TEST(ValidationAsyncClientTest, MockedMethods) {
  MessagePtr message{new RequestMessageImpl()};
  StrictMock<MockAsyncClientCallbacks> callbacks;
  StrictMock<MockAsyncClientStreamCallbacks> stream_callbacks;

  ValidationAsyncClient client;
  EXPECT_EQ(nullptr,
            client.send(std::move(message), callbacks, Optional<std::chrono::milliseconds>()));
  EXPECT_EQ(nullptr, client.start(stream_callbacks, Optional<std::chrono::milliseconds>()));
}

} // namespace Http
} // namespace Envoy
