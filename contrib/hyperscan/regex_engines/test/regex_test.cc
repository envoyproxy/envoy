#ifndef HYPERSCAN_DISABLED
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/hyperscan/regex_engines/source/regex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class EngineTest : public ::testing::Test {
protected:
  void setup() { engine_ = std::make_shared<HyperscanEngine>(dispatcher_, instance_); }

  void TearDown() override {
    instance_.shutdownGlobalThreading();
    ::testing::Test::TearDown();
  }

  Event::MockDispatcher dispatcher_;
  ThreadLocal::InstanceImpl instance_;
  std::shared_ptr<HyperscanEngine> engine_;
};

// Verify that the matcher can be populate successfully.
TEST_F(EngineTest, Matcher) {
  setup();

  EXPECT_TRUE(engine_->matcher("^/asdf/.+").status().ok());
}

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
#endif
