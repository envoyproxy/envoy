#include "server/init_manager_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/init/mocks.h"

#include "gmock/gmock.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;

namespace Envoy {
namespace Server {

class InitManagerImplTest : public testing::Test {
public:
  InitManagerImpl manager_;
  ReadyWatcher initialized_;
};

TEST_F(InitManagerImplTest, NoTargets) {
  EXPECT_CALL(initialized_, ready());
  manager_.initialize([&]() -> void { initialized_.ready(); });
}

TEST_F(InitManagerImplTest, Targets) {
  InSequence s;
  Init::MockTarget target;

  manager_.registerTarget(target);
  EXPECT_CALL(target, initialize(_));
  manager_.initialize([&]() -> void { initialized_.ready(); });
  EXPECT_CALL(initialized_, ready());
  target.callback_();
}

TEST_F(InitManagerImplTest, TargetRemoveWhileInitializing) {
  InSequence s;
  Init::MockTarget target;

  manager_.registerTarget(target);
  EXPECT_CALL(target, initialize(_)).WillOnce(Invoke([](std::function<void()> callback) -> void {
    callback();
  }));
  EXPECT_CALL(initialized_, ready());
  manager_.initialize([&]() -> void { initialized_.ready(); });
}

TEST_F(InitManagerImplTest, TargetAfterInitializing) {
  InSequence s;
  Init::MockTarget target1;
  Init::MockTarget target2;

  manager_.registerTarget(target1);
  EXPECT_CALL(target1, initialize(_));
  manager_.initialize([&]() -> void { initialized_.ready(); });

  EXPECT_CALL(target2, initialize(_));
  manager_.registerTarget(target2);

  target2.callback_();
  EXPECT_CALL(initialized_, ready());
  target1.callback_();
}

} // namespace Server
} // namespace Envoy
