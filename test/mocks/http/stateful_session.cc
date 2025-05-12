#include "test/mocks/http/stateful_session.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Http {

MockSessionStateFactory::MockSessionStateFactory() {
  ON_CALL(*this, create(_))
      .WillByDefault(
          Return(testing::ByMove(std::make_unique<testing::NiceMock<MockSessionState>>())));
}

MockSessionStateFactoryConfig::MockSessionStateFactoryConfig() {
  ON_CALL(*this, createSessionStateFactory(_, _))
      .WillByDefault(Return(std::make_shared<testing::NiceMock<MockSessionStateFactory>>()));
}

} // namespace Http
} // namespace Envoy
