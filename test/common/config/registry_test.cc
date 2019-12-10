#include <algorithm>

#include "envoy/registry/registry.h"

#include "common/common/fmt.h"

#include "test/test_common/logging.h"

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

  // Expect no version
  auto version =
      factories.find("testing.published")->second->getFactoryVersion("testing.published.test");
  EXPECT_FALSE(version.has_value());
}

class TestWithDeprecatedPublishedFactory : public PublishedFactory {
public:
  std::string name() override { return "testing.published.instead_name"; }
};

REGISTER_FACTORY(TestWithDeprecatedPublishedFactory,
                 PublishedFactory){"testing.published.deprecated_name"};

TEST(RegistryTest, WithDeprecatedFactoryPublished) {
  EXPECT_EQ("testing.published.instead_name",
            Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                "testing.published.deprecated_name")
                ->name());
  EXPECT_LOG_CONTAINS("warn",
                      fmt::format("{} is deprecated, use {} instead.",
                                  "testing.published.deprecated_name",
                                  "testing.published.instead_name"),
                      Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                          "testing.published.deprecated_name")
                          ->name());
}

class TestVersionedFactory : public PublishedFactory {
public:
  std::string name() override { return "testing.published.versioned"; }
  static envoy::api::v2::core::SemanticVersion getVersion() {
    envoy::api::v2::core::SemanticVersion version;
    version.set_major(2);
    version.set_minor(5);
    version.set_patch(39);
    return version;
  }
};

REGISTER_FACTORY(TestVersionedFactory, PublishedFactory)(TestVersionedFactory::getVersion());

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
  EXPECT_EQ(2, version.value().major());
  EXPECT_EQ(5, version.value().minor());
  EXPECT_EQ(39, version.value().patch());
}

} // namespace
} // namespace Config
} // namespace Envoy
