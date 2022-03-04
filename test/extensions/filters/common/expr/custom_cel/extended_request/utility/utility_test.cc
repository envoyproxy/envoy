#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

#include "absl/container/flat_hash_map.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {
namespace Utility {

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::ContainerBackedListImpl;

class UtilityTests : public testing::Test {};

TEST_F(UtilityTests, MergeListsTest) {
  Protobuf::Arena arena;
  ContainerBackedListImpl list{{
      CelValue::CreateStringView("foo"),
      CelValue::CreateStringView("bar"),
  }};

  // merge a list with itself
  CelList* merged_list = mergeLists(arena, &list, &list);
  // size of merged list should be equivalent to size of list
  EXPECT_EQ(list.size(), merged_list->size());

  auto cel_value_comparator = [](CelValue a, CelValue b) {
    return a.StringOrDie().value() < b.StringOrDie().value();
  };

  // add merged list to a set and check that every value of list
  // is in the merged list set
  std::set<CelValue, decltype(cel_value_comparator)> merged_list_set(cel_value_comparator);
  for (int i = 0; i < merged_list->size(); i++) {
    const auto& val = (*merged_list)[i];
    merged_list_set.emplace(val);
  }
  for (int i = 0; i < list.size(); i++) {
    int count = merged_list_set.count(list[i]);
    EXPECT_TRUE(count == 1);
  }
}

template <typename M> void mapTestImpl(M& map) {
  Protobuf::Arena arena;
  CelValue cel_value = Utility::createCelMap(arena, map);
  auto cel_map = cel_value.MapOrDie();
  auto value = (*cel_map)[CelValue::CreateStringView("fruit")]->StringOrDie().value();
  EXPECT_EQ(value, "apple");
  value = (*cel_map)[CelValue::CreateStringView("veg")]->StringOrDie().value();
  EXPECT_EQ(value, "carrot");
}

void mapTest(std::map<std::string, std::string> map) { mapTestImpl(map); }

void mapTest(absl::flat_hash_map<std::string, std::string> map) { mapTestImpl(map); }

TEST_F(UtilityTests, CreateCelMapTest) {
  absl::flat_hash_map<std::string, std::string> absl_map = {{"fruit", "apple"}, {"veg", "carrot"}};
  std::map<std::string, std::string> std_map = {{"fruit", "apple"}, {"veg", "carrot"}};

  mapTest(absl_map);
  mapTest(std_map);
}

} // namespace Utility
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
