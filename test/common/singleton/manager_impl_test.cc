#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "common/singleton/manager_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Singleton {

TEST(SingletonManagerImplTest, NotRegistered) {
  ManagerImpl manager;

  EXPECT_THROW_WITH_MESSAGE(manager.get("foo"), EnvoyException,
                            "invalid singleton name 'foo'. Make sure it is registered.");
}

static const char test_singleton_name[] = "test_singleton";
static Registry::RegisterFactory<Singleton::RegistrationImpl<test_singleton_name>,
                                 Singleton::Registration>
    test_singleton_registered_;

class TestSingleton : public Instance {
public:
  ~TestSingleton() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());
};

TEST(SingletonManagerImplTest, Basic) {
  ManagerImpl manager;

  EXPECT_EQ(nullptr, manager.get("test_singleton"));
  std::shared_ptr<TestSingleton> singleton = std::make_shared<TestSingleton>();
  manager.set("test_singleton", singleton);
  EXPECT_EQ(1UL, singleton.use_count());
  EXPECT_EQ(singleton, manager.get("test_singleton"));

  EXPECT_CALL(*singleton, onDestroy());
  singleton.reset();
  EXPECT_EQ(nullptr, manager.get("test_singleton"));
}

} // namespace Singleton
} // namespace Envoy
