#include "extensions/filters/network/http_connection_manager/dependency_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {
namespace {

using envoy::extensions::filters::common::dependency::v3::Dependency;
using envoy::extensions::filters::common::dependency::v3::FilterDependencies;
using testing::HasSubstr;

TEST(DependencyManagerTest, RegisterFilter) {
  DependencyManager manager;
  FilterDependencies dependencies;
  manager.registerFilter("foobar", dependencies);
  auto result = manager.validDecodeDependencies();
  EXPECT_TRUE(result.ok());
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

// Register five filters. The internal filter chain construct is modified
// iff at least one dependency is required or provided.
TEST(DependencyManagerTest, RegisterFilterIffNonEmptyDependencies) {
  DependencyManager manager;
  FilterDependencies dependencies;
  Dependency potato;
  potato.set_type(Dependency::FILTER_STATE_KEY);
  potato.set_name("potato");

  manager.registerFilter("a", dependencies);
  EXPECT_EQ(manager.filterChainSizeForTest(), 0);

  dependencies.Clear();
  *(dependencies.add_decode_required()) = potato;
  manager.registerFilter("b", dependencies);
  EXPECT_EQ(manager.filterChainSizeForTest(), 1);

  dependencies.Clear();
  *(dependencies.add_decode_provided()) = potato;
  manager.registerFilter("c", dependencies);
  EXPECT_EQ(manager.filterChainSizeForTest(), 2);

  dependencies.Clear();
  *(dependencies.add_encode_required()) = potato;
  manager.registerFilter("c", dependencies);
  EXPECT_EQ(manager.filterChainSizeForTest(), 3);

  dependencies.Clear();
  *(dependencies.add_encode_provided()) = potato;
  manager.registerFilter("d", dependencies);
  EXPECT_EQ(manager.filterChainSizeForTest(), 4);
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
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
