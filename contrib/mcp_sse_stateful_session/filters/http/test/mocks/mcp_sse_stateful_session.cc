#include "contrib/mcp_sse_stateful_session/filters/http/test/mocks/mcp_sse_stateful_session.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Http {

MockSseSessionStateFactory::MockSseSessionStateFactory() {
  ON_CALL(*this, create(_))
      .WillByDefault(
          Return(testing::ByMove(std::make_unique<testing::NiceMock<MockSseSessionState>>())));
}

MockSseSessionStateFactoryConfig::MockSseSessionStateFactoryConfig() {
  ON_CALL(*this, createSseSessionStateFactory(_, _))
      .WillByDefault(Return(std::make_shared<testing::NiceMock<MockSseSessionStateFactory>>()));
}

} // namespace Http
} // namespace Envoy
