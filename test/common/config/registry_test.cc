#include <algorithm>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class InternalFactory : public Config::UntypedFactory {
public:
  ~InternalFactory() override = default;
  std::string category() const override { return ""; }
};

class TestInternalFactory : public InternalFactory {
public:
  std::string name() const override { return "testing.internal.test"; }
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

class PublishedFactory : public Config::UntypedFactory {
public:
  ~PublishedFactory() override = default;
  std::string category() const override { return "testing.published"; }
};

class TestPublishedFactory : public PublishedFactory {
public:
  std::string name() const override { return "testing.published.test"; }
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

  // Expect no version
  auto const version =
      factories.find("testing.published")->second->getFactoryVersion("testing.published.test");
  EXPECT_FALSE(version.has_value());
}

class TestVersionedFactory : public PublishedFactory {
public:
  std::string name() const override { return "testing.published.versioned"; }
};

REGISTER_FACTORY(TestVersionedFactory,
                 PublishedFactory){FACTORY_VERSION(2, 5, 39, {{"build.label", "alpha"}})};

// Test registration of versioned factory
TEST(RegistryTest, VersionedFactory) {
  const auto& factories = Envoy::Registry::FactoryCategoryRegistry::registeredFactories();

  // Expect that the category is present.
  ASSERT_NE(factories.find("testing.published"), factories.end());

  // Expect that the factory is listed in the right category.
  const auto& names = factories.find("testing.published")->second->registeredNames();
  EXPECT_NE(std::find(names.begin(), names.end(), "testing.published.versioned"), std::end(names));

  // Expect that the factory is present.
  EXPECT_NE(Registry::FactoryRegistry<PublishedFactory>::getFactory("testing.published.versioned"),
            nullptr);

  auto version =
      factories.find("testing.published")->second->getFactoryVersion("testing.published.versioned");
  EXPECT_TRUE(version.has_value());
  EXPECT_EQ(2, version.value().version().major_number());
  EXPECT_EQ(5, version.value().version().minor_number());
  EXPECT_EQ(39, version.value().version().patch());
  EXPECT_EQ(1, version.value().metadata().fields().size());
  EXPECT_EQ("alpha", version.value().metadata().fields().at("build.label").string_value());
}

TEST(RegistryTest, TestDoubleRegistrationByName) {
  EXPECT_THROW_WITH_MESSAGE((Registry::RegisterFactory<TestPublishedFactory, PublishedFactory>()),
                            EnvoyException,
                            "Double registration for name: 'testing.published.test'");
}

} // namespace
} // namespace Config
} // namespace Envoy
