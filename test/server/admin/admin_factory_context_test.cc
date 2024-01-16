#include "source/common/listener_manager/listener_info_impl.h"
#include "source/server/admin/admin_factory_context.h"

#include "test/mocks/server/instance.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(AdminFactoryContextTest, AdminFactoryContextTest) {
  testing::NiceMock<MockInstance> server;
  const auto listener_info = std::make_shared<ListenerInfoImpl>();

  AdminFactoryContext context(server, listener_info);

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
