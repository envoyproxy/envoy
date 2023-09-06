#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/server/hot_restarting_child.h"
#include "source/server/hot_restarting_parent.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_manager.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class HotRestartingChildTest : public testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(2);
    EXPECT_CALL(os_sys_calls_, close(_)).Times(2);
    hot_restarting_child_ = std::make_unique<HotRestartingChild>(0, 1, "@envoy_domain_socket", 0);
    EXPECT_CALL(dispatcher_, createFileEvent(_, _, _, Event::FileReadyType::Read))
        .WillOnce(SaveArg<1>(file_ready_callback_));
    hot_restarting_child_->initialize(dispatcher_);
  }
  void TearDown() override { hot_restarting_child_.reset(); }
  Api::MockOsSysCalls os_sys_calls_;
  Event::MockDispatcher dispatcher_;
  Event::FileReadyCb file_ready_callback_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  std::unique_ptr<HotRestartingChild> hot_restarting_child_;
};

TEST_F(HotRestartingChildTest, Something) {}

} // namespace
} // namespace Server
} // namespace Envoy
