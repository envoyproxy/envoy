#include "source/common/local_info/local_info_impl.h"

#include "test/common/stats/stat_test_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace LocalInfo {
namespace {

TEST(LocalInfoTest, DynamicContextUpdate) {
  Stats::TestUtil::TestSymbolTable symbol_table;
  LocalInfoImpl local_info(*symbol_table, {}, {}, nullptr, "zone_name", "cluster_name",
                           "node_name");
  local_info.contextProvider().setDynamicContextParam("foo", "bar", "baz");
  EXPECT_EQ("baz", local_info.node().dynamic_parameters().at("foo").params().at("bar"));
}

} // namespace
} // namespace LocalInfo
} // namespace Envoy
