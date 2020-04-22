#include "envoy/registry/registry.h"

#include "common/singleton/manager_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Singleton {
namespace {

// Must be a dedicated function so that TID is within the death test.
static void deathTestWorker() {
  ManagerImpl manager(Thread::threadFactoryForTest());

  manager.get("foo", [] { return nullptr; });
}

TEST(SingletonManagerImplDeathTest, NotRegistered) {
  EXPECT_DEATH_LOG_TO_STDERR(deathTestWorker(),
                             "invalid singleton name 'foo'. Make sure it is registered.");
}

SINGLETON_MANAGER_REGISTRATION(test);

class TestSingleton : public Instance {
public:
  ~TestSingleton() override { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());
};

TEST(SingletonManagerImplTest, Basic) {
  ManagerImpl manager(Thread::threadFactoryForTest());

  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  EXPECT_EQ(singleton, manager.get("test_singleton", [singleton] { return singleton; }));
  EXPECT_EQ(1UL, singleton.use_count());
  EXPECT_EQ(singleton, manager.get("test_singleton", [] { return nullptr; }));

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
}

} // namespace
} // namespace Singleton
} // namespace Envoy
