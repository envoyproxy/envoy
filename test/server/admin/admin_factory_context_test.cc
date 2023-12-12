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
  context.listenerInfo().isQuic();
  context.listenerInfo().metadata();
  context.listenerInfo().typedMetadata();
  context.listenerInfo().direction();
  context.messageValidationVisitor();
  context.initManager();
  context.drainDecision();
}

} // namespace
} // namespace Server
} // namespace Envoy
