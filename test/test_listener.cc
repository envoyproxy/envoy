#include "test/test_listener.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

#include "test/test_common/global.h"

namespace Envoy {

void TestListener::OnTestEnd(const ::testing::TestInfo& test_info) {
  if (validate_singletons_) {
    // Check that all singletons have been destroyed.
    std::string active_singletons = Envoy::Test::Globals::describeActiveSingletons();
    /*    RELEASE_ASSERT(active_singletons.empty(),
                       absl::StrCat("FAIL [", test_info.test_suite_name(), ".", test_info.name(),
                                    "]: Active singletons exist. Something is leaking. Consider "
                                    "commenting out this assert and letting the heap checker
       run:\n", active_singletons));*/
    RELEASE_ASSERT(!Thread::MainThread::isMainThreadActive(),
                   absl::StrCat("MainThreadLeak: [", test_info.test_suite_name(), ".",
                                test_info.name(), "] test exited before main thread shut down"));
  }
  // We must do this in two phases; reset the old flags to get back to the clean state before
  // constructing a new flag saver to latch the clean values.
  saver_.reset();
  saver_ = std::make_unique<absl::FlagSaver>();
}

} // namespace Envoy
