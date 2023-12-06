#include "source/server/admin/admin_factory_context.h"

#include "test/mocks/server/instance.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(AdminFactoryContextTest, AdminFactoryContextTest) {
  testing::NiceMock<MockInstance> server;

  AdminFactoryContext context(server);

  context.serverFactoryContext();
  context.getTransportSocketFactoryContext();
  context.scope();
  context.listenerScope();
  context.isQuicListener();
  context.listenerMetadata();
  context.listenerTypedMetadata();
  context.direction();
  context.messageValidationVisitor();
  context.initManager();
  context.drainDecision();
}

} // namespace
} // namespace Server
} // namespace Envoy
