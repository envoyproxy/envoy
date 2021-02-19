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
  EXPECT_TRUE(manager.decodePathIsValid());
}

TEST(DependencyManagerTest, RegisterFilterWithUnmetRequirement) {
  DependencyManager manager;

  FilterDependencies dependencies;
  auto required = dependencies.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");
  manager.registerFilter("chef", dependencies);
  EXPECT_FALSE(manager.decodePathIsValid());
}

TEST(DependencyManagerTest, DependencySatisfied) {
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

  EXPECT_TRUE(manager.decodePathIsValid());
}

TEST(DependencyManagerTest, MisorderedDependencyNotSatisfied) {
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

  EXPECT_FALSE(manager.decodePathIsValid());
}

// TODO(auni53): Add unit tests validating dependency type match.

} // namespace
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
