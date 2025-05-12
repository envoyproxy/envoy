#pragma once

#include "envoy/server/health_checker_config.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"
#include "test/mocks/upstream/health_checker.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class MockHealthCheckerFactoryContext : public virtual HealthCheckerFactoryContext {
public:
  MockHealthCheckerFactoryContext();
  ~MockHealthCheckerFactoryContext() override;

  MOCK_METHOD(Upstream::Cluster&, cluster, ());
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
  MOCK_METHOD(Envoy::Random::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(void, setEventLogger, (Upstream::HealthCheckEventLoggerPtr));
  MOCK_METHOD(Server::Configuration::ServerFactoryContext&, serverFactoryContext, ());

  Upstream::HealthCheckEventLoggerPtr eventLogger() override {
    if (!event_logger_) {
      event_logger_ = std::make_unique<testing::NiceMock<Upstream::MockHealthCheckEventLogger>>();
    }
    return std::move(event_logger_);
  }

  testing::NiceMock<Upstream::MockClusterMockPrioritySet> cluster_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<Envoy::Random::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_;
  testing::NiceMock<Envoy::Api::MockApi> api_{};
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  std::unique_ptr<testing::NiceMock<Envoy::Upstream::MockHealthCheckEventLogger>> event_logger_;
  testing::NiceMock<MockServerFactoryContext> server_context_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
