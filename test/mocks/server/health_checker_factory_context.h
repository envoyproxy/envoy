#pragma once

#include "envoy/server/health_checker_config.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockHealthCheckerFactoryContext : public virtual HealthCheckerFactoryContext {
public:
  MockHealthCheckerFactoryContext();
  ~MockHealthCheckerFactoryContext() override;

  MOCK_METHOD(Upstream::Cluster&, cluster, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Envoy::Runtime::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Upstream::HealthCheckEventLogger*, eventLogger_, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  Upstream::HealthCheckEventLoggerPtr eventLogger() override {
    return Upstream::HealthCheckEventLoggerPtr(eventLogger_());
  }

  testing::NiceMock<Upstream::MockClusterMockPrioritySet> cluster_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Envoy::Runtime::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_;
  testing::NiceMock<Envoy::Upstream::MockHealthCheckEventLogger>* event_logger_{};
  testing::NiceMock<Envoy::Api::MockApi> api_{};
};
} // namespace Configuration

} // namespace Server
} // namespace Envoy
