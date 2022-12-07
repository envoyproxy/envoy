#include "health_checker_factory_context.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockHealthCheckerFactoryContext::MockHealthCheckerFactoryContext() {
  ON_CALL(*this, cluster()).WillByDefault(ReturnRef(cluster_));
  ON_CALL(*this, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, random()).WillByDefault(ReturnRef(random_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
}

MockHealthCheckerFactoryContext::~MockHealthCheckerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
