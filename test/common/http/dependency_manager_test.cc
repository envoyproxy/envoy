#include "source/common/http/dependency_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

using envoy::extensions::filters::common::dependency::v3::Dependency;
using envoy::extensions::filters::common::dependency::v3::FilterDependencies;
using testing::HasSubstr;

TEST(DependencyManagerTest, RegisterFilter) {
  DependencyManager manager;

  FilterDependencies dependencies;
  manager.registerFilter("foobar", dependencies);
}

TEST(DependencyManagerTest, RegisterFilterWithDependency) {
  DependencyManager manager;

  FilterDependencies dependencies;
  auto provided = dependencies.add_decode_provided();
  provided->set_type(Dependency::FILTER_STATE_KEY);
  provided->set_name("potato");
  manager.registerFilter("ingredient", dependencies);
  auto result = manager.validDecodeDependencies();
  EXPECT_TRUE(result.ok());
}

TEST(DependencyManagerTest, Valid) {
  DependencyManager manager;

  FilterDependencies d1;
  auto provided = d1.add_decode_provided();
  provided->set_type(Dependency::FILTER_STATE_KEY);
  provided->set_name("potato");

  FilterDependencies d2;
  auto required = d2.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");

  manager.registerFilter("ingredient", d1);
  manager.registerFilter("chef", d2);

  auto result = manager.validDecodeDependencies();
  EXPECT_TRUE(result.ok());
}

TEST(DependencyManagerTest, MissingProvidencyInvalid) {
  DependencyManager manager;

  FilterDependencies dependencies;
  auto required = dependencies.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");
  manager.registerFilter("chef", dependencies);

  auto result = manager.validDecodeDependencies();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.message(),
              HasSubstr("filter 'chef' requires a FILTER_STATE_KEY named 'potato'"));
}

TEST(DependencyManagerTest, WrongProvidencyTypeInvalid) {
  DependencyManager manager;

  FilterDependencies d1;
  auto provided = d1.add_decode_provided();
  provided->set_type(Dependency::HEADER);
  provided->set_name("potato");

  FilterDependencies d2;
  auto required = d2.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");

  manager.registerFilter("ingredient", d1);
  manager.registerFilter("chef", d2);

  auto result = manager.validDecodeDependencies();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.message(),
              HasSubstr("filter 'chef' requires a FILTER_STATE_KEY named 'potato'"));
}

TEST(DependencyManagerTest, WrongProvidencyNameInvalid) {
  DependencyManager manager;

  FilterDependencies d1;
  auto provided = d1.add_decode_provided();
  provided->set_type(Dependency::FILTER_STATE_KEY);
  provided->set_name("tomato");

  FilterDependencies d2;
  auto required = d2.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");

  manager.registerFilter("ingredient", d1);
  manager.registerFilter("chef", d2);

  auto result = manager.validDecodeDependencies();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.message(),
              HasSubstr("filter 'chef' requires a FILTER_STATE_KEY named 'potato'"));
}

TEST(DependencyManagerTest, MisorderedFiltersInvalid) {
  DependencyManager manager;

  FilterDependencies d1;
  auto provided = d1.add_decode_provided();
  provided->set_type(Dependency::FILTER_STATE_KEY);
  provided->set_name("potato");

  FilterDependencies d2;
  auto required = d2.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");

  manager.registerFilter("chef", d2);
  manager.registerFilter("ingredient", d1);

  auto result = manager.validDecodeDependencies();
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.message(),
              HasSubstr("filter 'chef' requires a FILTER_STATE_KEY named 'potato'"));
}

} // namespace
} // namespace Http
} // namespace Envoy
