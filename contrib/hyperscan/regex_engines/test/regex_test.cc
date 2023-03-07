#ifndef HYPERSCAN_DISABLED
#include "source/common/thread_local/thread_local_impl.h"

#include "test/test_common/utility.h"

#include "contrib/hyperscan/regex_engines/source/regex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class EngineTest : public ::testing::Test {
protected:
  void setup() { engine_ = std::make_shared<HyperscanEngine>(instance_); }

  void TearDown() override {
    instance_.shutdownGlobalThreading();
    ::testing::Test::TearDown();
  }

  ThreadLocal::InstanceImpl instance_;
  std::shared_ptr<HyperscanEngine> engine_;
};

// Verify that the matcher can be populate successfully.
TEST_F(EngineTest, Matcher) {
  setup();

  EXPECT_NO_THROW(engine_->matcher("^/asdf/.+"));
}

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
#endif
