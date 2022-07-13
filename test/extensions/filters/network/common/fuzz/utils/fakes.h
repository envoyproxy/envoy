#pragma once

#include "test/mocks/server/factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class FakeFactoryContext : public MockFactoryContext {
public:
 void prepareSimulatedSystemTime() {
   api_ = Api::createApiForTest(time_system_);
   dispatcher_ = api_->allocateDispatcher("test_thread");
   ON_CALL(mock_server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
   ON_CALL(mock_server_context_, mainThreadDispatcher()).WillByDefault(testing::ReturnRef(*dispatcher_));
   ON_CALL(mock_server_context_, timeSource()).WillByDefault(testing::ReturnRef(time_system_));
 }
  Event::SimulatedTimeSystem& simulatedTimeSystem() {
      return dynamic_cast<Event::SimulatedTimeSystem&>(time_system_);
    }

 Event::DispatcherPtr dispatcher_;
 Event::SimulatedTimeSystem time_system_;
 Api::ApiPtr api_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
