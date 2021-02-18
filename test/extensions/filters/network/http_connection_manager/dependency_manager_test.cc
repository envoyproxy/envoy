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
  manager.RegisterFilter("foobar", dependencies);
}

TEST(DependencyManagerTest, RegisterFilterWithDependency) {
  DependencyManager manager;

  FilterDependencies dependencies;
  auto provided = dependencies.add_decode_provided();
  provided->set_type(Dependency::FILTER_STATE_KEY);
  provided->set_name("potato");
  manager.RegisterFilter("ingredient", dependencies);
  EXPECT_TRUE(manager.IsValid());
}

TEST(DependencyManagerTest, RegisterFilterWithUnmetRequirement) {
  DependencyManager manager;

  FilterDependencies dependencies;
  auto required = dependencies.add_decode_required();
  required->set_type(Dependency::FILTER_STATE_KEY);
  required->set_name("potato");
  manager.RegisterFilter("chef", dependencies);
  EXPECT_FALSE(manager.IsValid());
}

} // namespace
} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
