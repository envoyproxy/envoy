#include <algorithm>

#include "envoy/registry/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class InternalFactory {
public:
  virtual ~InternalFactory() = default;
  virtual std::string name() PURE;
};

class TestInternalFactory : public InternalFactory {
public:
  std::string name() override { return "testing.internal.test"; }
};

static Registry::RegisterInternalFactory<TestInternalFactory, InternalFactory>
    test_internal_register;

// Ensure that the internal test factory name isn't visible from a
// published category. Note that the internal factory can't be in
// a registered category by definition since it doesn't have a static
// category method.
TEST(RegistryTest, InternalFactoryNotPublished) {
  TestInternalFactory test;

  // Expect that the published categories don't lead to the internal factory.
  for (const auto& ext : Envoy::Registry::FactoryCategoryRegistry::registeredFactories()) {
    for (const auto& name : ext.second->registeredNames()) {
      EXPECT_NE(name, test.name());
    }
  }

  // Expect that the factory is present.
  EXPECT_NE(Registry::FactoryRegistry<InternalFactory>::getFactory("testing.internal.test"),
            nullptr);
}

class PublishedFactory {
public:
  virtual ~PublishedFactory() = default;
  virtual std::string name() PURE;
  static std::string category() { return "testing.published"; }
};

class TestPublishedFactory : public PublishedFactory {
public:
  std::string name() override { return "testing.published.test"; }
};

REGISTER_FACTORY(TestPublishedFactory, PublishedFactory);

TEST(RegistryTest, DefaultFactoryPublished) {
  const auto& factories = Envoy::Registry::FactoryCategoryRegistry::registeredFactories();

  // Expect that the category is present.
  ASSERT_NE(factories.find("testing.published"), factories.end());

  // Expect that the factory is listed in the right category.
  const auto& names = factories.find("testing.published")->second->registeredNames();
  EXPECT_NE(std::find(names.begin(), names.end(), "testing.published.test"), std::end(names));

  // Expect that the factory is present.
  EXPECT_NE(Registry::FactoryRegistry<PublishedFactory>::getFactory("testing.published.test"),
            nullptr);
}

} // namespace
} // namespace Config
} // namespace Envoy
