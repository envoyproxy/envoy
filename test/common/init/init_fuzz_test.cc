#include "common/common/assert.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"
#include "common/init/watcher_impl.h"

#include "test/common/init/init_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Fuzz {
namespace {
using namespace Init;

class InitDriver {
public:
  InitDriver(int manager_size, int target_size) {
    for (int i = 0; i < manager_size; i++) {
      ms_.emplace_back(std::make_unique<ManagerImpl>(absl::StrCat("fuzz_manager_", i)));
      ws_.emplace_back(std::make_unique<WatcherImpl>(
          absl::StrCat("fuzz_watcher_", i), [this, i]() mutable { ready_flags_[i] = true; }));
      ready_flags_.emplace_back(false);
    }
    for (int i = 0; i < target_size; i++) {
      ts_.emplace_back(std::make_unique<TargetImpl>(absl::StrCat("fuzz_target_", i), []() {}));
    }
  }

  void transit(const test::common::init::InitAction& action) {
    auto param = action.param();
    int mid = (param % 1);
    int tid = (param % 1);
    switch (action.action()) {
    case test::common::init::INIT_MANAGER_INITIALIZE:
      ms_[mid]->initialize(*ws_[mid]);
      break;
    case test::common::init::INIT_MANAGER_ADD_TARGET:

      ms_[mid]->add(*ts_[tid]);
      break;
    case test::common::init::INIT_TARGET_READY:

      ts_[tid]->ready();
    default:
      // NOT_IMPLEMENT_YET
      break;
    }
  };

  std::vector<bool> ready_flags_;
  std::vector<std::unique_ptr<ManagerImpl>> ms_;
  std::vector<std::unique_ptr<TargetImpl>> ts_;
  std::vector<std::unique_ptr<WatcherImpl>> ws_;
};

DEFINE_PROTO_FUZZER(const test::common::init::InitTestCase& input) {
  if (input.actions_size() < 3) {
    return;
  }
  InitDriver driver(1, 1);
  ENVOY_LOG_MISC(critical, "input size {}", input.actions_size());
  for (const auto& act : input.actions()) {
    driver.transit(act);
  }
  RELEASE_ASSERT(driver.ready_flags_[0], "manager not initialized");
}
} // namespace
} // namespace Fuzz
} // namespace Envoy