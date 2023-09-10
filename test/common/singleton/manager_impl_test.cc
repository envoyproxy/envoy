#include "envoy/registry/registry.h"

#include "source/common/singleton/manager_impl.h"

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
  EXPECT_ENVOY_BUG(deathTestWorker(), "invalid singleton name 'foo'. Make sure it is registered.");
}

SINGLETON_MANAGER_REGISTRATION(test);

class TestSingleton : public Instance {
public:
  ~TestSingleton() override { onDestroy(); }

  MOCK_METHOD(void, onDestroy, ());
};

TEST(SingletonRegistration, category) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Singleton::Registration>::getFactory(test_singleton_name);
  EXPECT_TRUE(factory != nullptr);
  EXPECT_EQ("envoy.singleton", factory->category());
}

TEST(SingletonManagerImplTest, Basic) {
  ManagerImpl manager(Thread::threadFactoryForTest());

  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  EXPECT_EQ(singleton, manager.get("test_singleton", [singleton] { return singleton; }));
  EXPECT_EQ(1UL, singleton.use_count());
  EXPECT_EQ(singleton, manager.get("test_singleton", [] { return nullptr; }));

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
}

TEST(SingletonManagerImplTest, NonConstructingGetTyped) {
  ManagerImpl manager(Thread::threadFactoryForTest());

  // Access without first constructing should be null.
  EXPECT_EQ(nullptr, manager.getTyped<TestSingleton>("test_singleton"));

  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  // Use a construct on first use getter.
  EXPECT_EQ(singleton, manager.get("test_singleton", [singleton] { return singleton; }));
  // Now access should return the constructed singleton.
  EXPECT_EQ(singleton, manager.getTyped<TestSingleton>("test_singleton"));
  EXPECT_EQ(1UL, singleton.use_count());

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
}

} // namespace
} // namespace Singleton
} // namespace Envoy
