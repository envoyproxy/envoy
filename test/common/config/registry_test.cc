#include <algorithm>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "common/common/fmt.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

using ::testing::Optional;

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

class TestWithDeprecatedPublishedFactory : public PublishedFactory {
public:
  std::string name() const override { return "testing.published.instead_name"; }
};

REGISTER_FACTORY(TestWithDeprecatedPublishedFactory,
                 PublishedFactory){"testing.published.deprecated_name"};

TEST(RegistryTest, DEPRECATED_FEATURE_TEST(WithDeprecatedFactoryPublished)) {
  EXPECT_EQ("testing.published.instead_name",
            Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                "testing.published.deprecated_name")
                ->name());
  EXPECT_LOG_CONTAINS("warn",
                      fmt::format("Using deprecated extension name '{}' for '{}'.",
                                  "testing.published.deprecated_name",
                                  "testing.published.instead_name"),
                      Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                          "testing.published.deprecated_name")
                          ->name());
}

class NoNamePublishedFactory : public PublishedFactory {
public:
  std::string name() const override { return ""; }
};

TEST(RegistryTest, DEPRECATED_FEATURE_TEST(AssertsIfNoDeprecatedNameGiven)) {
  // Expects an assert to raise if we register a factory that has an empty name
  // and no associated deprecated names.
  EXPECT_DEBUG_DEATH((Registry::RegisterFactory<NoNamePublishedFactory, PublishedFactory>({})),
                     "Attempted to register a factory without a name or deprecated name");
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

class TestVersionedWithDeprecatedNamesFactory : public PublishedFactory {
public:
  std::string name() const override { return "testing.published.versioned.instead_name"; }
};

REGISTER_FACTORY(TestVersionedWithDeprecatedNamesFactory,
                 PublishedFactory){FACTORY_VERSION(0, 0, 1, {{"build.kind", "private"}}),
                                   {"testing.published.versioned.deprecated_name"}};

// Test registration of versioned factory that also uses deprecated names
TEST(RegistryTest, DEPRECATED_FEATURE_TEST(VersionedWithDeprecatedNamesFactory)) {
  EXPECT_EQ("testing.published.versioned.instead_name",
            Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                "testing.published.versioned.deprecated_name")
                ->name());
  EXPECT_LOG_CONTAINS("warn",
                      fmt::format("Using deprecated extension name '{}' for '{}'.",
                                  "testing.published.versioned.deprecated_name",
                                  "testing.published.versioned.instead_name"),
                      Envoy::Registry::FactoryRegistry<PublishedFactory>::getFactory(
                          "testing.published.versioned.deprecated_name")
                          ->name());
  const auto& factories = Envoy::Registry::FactoryCategoryRegistry::registeredFactories();
  auto version = factories.find("testing.published")
                     ->second->getFactoryVersion("testing.published.versioned.instead_name");
  EXPECT_TRUE(version.has_value());
  EXPECT_EQ(0, version.value().version().major_number());
  EXPECT_EQ(0, version.value().version().minor_number());
  EXPECT_EQ(1, version.value().version().patch());
  EXPECT_EQ(1, version.value().metadata().fields().size());
  EXPECT_EQ("private", version.value().metadata().fields().at("build.kind").string_value());
  // Get the version using deprecated name and check that it matches the
  // version obtained through the new name.
  auto deprecated_version =
      factories.find("testing.published")
          ->second->getFactoryVersion("testing.published.versioned.deprecated_name");
  EXPECT_TRUE(deprecated_version.has_value());
  EXPECT_THAT(deprecated_version.value(), ProtoEq(version.value()));
}

TEST(RegistryTest, TestDoubleRegistrationByName) {
  EXPECT_THROW_WITH_MESSAGE((Registry::RegisterFactory<TestPublishedFactory, PublishedFactory>()),
                            EnvoyException,
                            "Double registration for name: 'testing.published.test'");
}

class PublishedFactoryWithNameAndCategory : public PublishedFactory {
public:
  std::string category() const override { return "testing.published.additional.category"; }
  std::string name() const override {
    return "testing.published.versioned.instead_name_and_category";
  }
};

TEST(RegistryTest, DEPRECATED_FEATURE_TEST(VersionedWithDeprecatedNamesFactoryAndNewCategory)) {
  PublishedFactoryWithNameAndCategory test;

  // Check the category is not registered
  ASSERT_FALSE(Registry::FactoryCategoryRegistry::isRegistered(test.category()));

  auto factory = Registry::RegisterFactory<PublishedFactoryWithNameAndCategory, PublishedFactory>(
      FACTORY_VERSION(0, 0, 1, {{"build.kind", "private"}}),
      {"testing.published.versioned.deprecated_name_and_category"});

  // Check the category now registered
  ASSERT_TRUE(Registry::FactoryCategoryRegistry::isRegistered(test.category()));

  const auto& factories = Envoy::Registry::FactoryCategoryRegistry::registeredFactories();

  auto version =
      factories.find("testing.published.additional.category")
          ->second->getFactoryVersion("testing.published.versioned.instead_name_and_category");

  ASSERT_TRUE(version.has_value());
  EXPECT_EQ(0, version.value().version().major_number());
  EXPECT_EQ(0, version.value().version().minor_number());
  EXPECT_EQ(1, version.value().version().patch());
  EXPECT_EQ(1, version.value().metadata().fields().size());
  EXPECT_EQ("private", version.value().metadata().fields().at("build.kind").string_value());

  // Get the version using deprecated name and check that it matches the
  // version obtained through the new name.
  auto deprecated_version =
      factories.find("testing.published.additional.category")
          ->second->getFactoryVersion("testing.published.versioned.deprecated_name_and_category");
  EXPECT_THAT(deprecated_version, Optional(ProtoEq(version.value())));
}

} // namespace
} // namespace Config
} // namespace Envoy
