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

  context.accessLogManager();
  context.clusterManager();
  context.mainThreadDispatcher();
  context.options();
  context.grpcContext();
  context.healthCheckFailed();
  context.httpContext();
  context.routerContext();
  context.localInfo();
  context.runtime();
  context.serverScope();
  context.messageValidationContext();
  context.singletonManager();
  context.overloadManager();
  context.threadLocal();
  context.admin();
  context.timeSource();
  context.api();
  context.lifecycleNotifier();
  context.processContext();
}

} // namespace
} // namespace Server
} // namespace Envoy
