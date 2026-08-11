#pragma once

#include <memory>

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using testing::NiceMock;
using testing::ReturnRef;

class HealthCheckerTestBase {
public:
  HealthCheckerTestBase() {
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(context_.api_, randomGenerator()).WillByDefault(ReturnRef(random_));
    ON_CALL(context_, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(context_.server_context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(context_.server_context_.api_, randomGenerator()).WillByDefault(ReturnRef(random_));
    ON_CALL(context_.server_context_, runtime()).WillByDefault(ReturnRef(runtime_));
  }
  std::shared_ptr<MockClusterMockPrioritySet> cluster_{
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<MockHealthCheckEventLogger> event_logger_storage_{
      std::make_unique<MockHealthCheckEventLogger>()};
  MockHealthCheckEventLogger& event_logger_{*event_logger_storage_};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context_;
};

} // namespace Upstream
} // namespace Envoy
