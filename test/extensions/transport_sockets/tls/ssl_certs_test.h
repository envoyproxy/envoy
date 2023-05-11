#pragma once

#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
class SslCertsTest : public testing::Test {
protected:
  SslCertsTest() : api_(Api::createApiForTest(store_, time_system_)) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  Event::SimulatedTimeSystem time_system_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  Api::ApiPtr api_;
};
} // namespace Envoy
