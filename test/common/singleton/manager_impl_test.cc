#include "envoy/registry/registry.h"

#include "source/common/singleton/manager_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Singleton {
namespace {

// Must be a dedicated function so that TID is within the death test.
static void deathTestWorker() {
  ManagerImpl manager;

  manager.get(
      "foo", [] { return nullptr; }, false);
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
  ManagerImpl manager;

  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  EXPECT_EQ(singleton, manager.get(
                           "test_singleton", [singleton] { return singleton; }, false));
  EXPECT_EQ(1UL, singleton.use_count());
  EXPECT_EQ(singleton, manager.get(
                           "test_singleton", [] { return nullptr; }, false));

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
}

TEST(SingletonManagerImplTest, NonConstructingGetTyped) {
  ManagerImpl manager;

  // Access without first constructing should be null.
  EXPECT_EQ(nullptr, manager.getTyped<TestSingleton>("test_singleton"));

  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  // Use a construct on first use getter.
  EXPECT_EQ(singleton, manager.get(
                           "test_singleton", [singleton] { return singleton; }, false));
  // Now access should return the constructed singleton.
  EXPECT_EQ(singleton, manager.getTyped<TestSingleton>("test_singleton"));
  EXPECT_EQ(1UL, singleton.use_count());

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
}

TEST(SingletonManagerImplTest, PinnedSingleton) {

  {
    ManagerImpl manager;
    TestSingleton* singleton_ptr{};

    // Register a singleton and get it.
    auto singleton = manager.getTyped<TestSingleton>(SINGLETON_MANAGER_REGISTERED_NAME(test),
                                                     [&]() -> InstanceSharedPtr {
                                                       auto s = std::make_shared<TestSingleton>();
                                                       singleton_ptr = s.get();
                                                       return s;
                                                     });
    EXPECT_EQ(singleton, manager.getTyped<TestSingleton>(SINGLETON_MANAGER_REGISTERED_NAME(test)));

    EXPECT_CALL(*singleton_ptr, onDestroy());
    // Destroy all copies of the singleton shared pointer.
    singleton.reset();

    // The singleton should be destroyed now.
    EXPECT_EQ(nullptr, manager.getTyped<TestSingleton>(SINGLETON_MANAGER_REGISTERED_NAME(test)));
  }

  {
    ManagerImpl manager;
    TestSingleton* singleton_ptr{};

    // Register a pinned singleton and get it.
    auto singleton = manager.getTyped<TestSingleton>(
        SINGLETON_MANAGER_REGISTERED_NAME(test),
        [&]() -> InstanceSharedPtr {
          auto s = std::make_shared<TestSingleton>();
          singleton_ptr = s.get();
          return s;
        },
        true);
    EXPECT_EQ(singleton, manager.getTyped<TestSingleton>(SINGLETON_MANAGER_REGISTERED_NAME(test)));

    auto* expected_value = singleton.get();

    // Destroy all copies of the singleton shared pointer.
    singleton.reset();

    // The singleton should still be available.
    EXPECT_EQ(expected_value,
              manager.getTyped<TestSingleton>(SINGLETON_MANAGER_REGISTERED_NAME(test)).get());

    // Destroy the singleton after the manager is destroyed.
    EXPECT_CALL(*singleton_ptr, onDestroy());
  }
}

} // namespace
} // namespace Singleton
} // namespace Envoy
