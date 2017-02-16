#include "server/server.h"

#include "test/mocks/common.h"
#include "test/mocks/init/mocks.h"

using testing::_;
using testing::InSequence;

namespace Server {

TEST(InitManagerImplTest, NoTargets) {
  InitManagerImpl manager;
  ReadyWatcher initialized;

  EXPECT_CALL(initialized, ready());
  manager.initialize([&]() -> void { initialized.ready(); });
}

TEST(InitManagerImplTest, Targets) {
  InSequence s;
  InitManagerImpl manager;
  Init::MockTarget target;
  ReadyWatcher initialized;

  manager.registerTarget(target);
  EXPECT_CALL(target, initialize(_));
  manager.initialize([&]() -> void { initialized.ready(); });
  EXPECT_CALL(initialized, ready());
  target.callback_();
}

} // Server
