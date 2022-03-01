#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

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

TEST_F(UtilityTests, AppendListTest) {
  Protobuf::Arena arena;
  ContainerBackedListImpl list1{{
      CelValue::CreateStringView("foo"),
  }};
  ContainerBackedListImpl list2{{
      CelValue::CreateStringView("bar"),
  }};

  CelList* list3 = appendList(arena, &list1, &list2);
  ASSERT_EQ(list1.size() + list2.size(), list3->size());

  for (int i = 0; i < list1.size(); i++) {
    auto list1_val = list1[i].StringOrDie().value();
    auto list3_val = (*list3)[i].StringOrDie().value();
    ASSERT_EQ(list1_val, list3_val);
  }
  for (int j = 0; j < list2.size(); j++) {
    auto list2_val = list2[j].StringOrDie().value();
    auto list3_val = (*list3)[list1.size() + j].StringOrDie().value();
    ASSERT_EQ(list2_val, list3_val);
  }
}

TEST_F(UtilityTests, CreateCelMapTest) {
  Protobuf::Arena arena;
  std::vector<std::pair<CelValue, CelValue>> key_value_pairs;
  auto fruit =
      std::make_pair(CelValue::CreateStringView("fruit"), CelValue::CreateStringView("apple"));
  auto veg =
      std::make_pair(CelValue::CreateStringView("veg"), CelValue::CreateStringView("carrot"));

  key_value_pairs.push_back(fruit);
  key_value_pairs.push_back(veg);
  CelValue cel_value = Utility::createCelMap(arena, key_value_pairs);
  auto cel_map = cel_value.MapOrDie();
  auto value = (*cel_map)[CelValue::CreateStringView("fruit")]->StringOrDie().value();
  ASSERT_EQ(value, "apple");
  value = (*cel_map)[CelValue::CreateStringView("veg")]->StringOrDie().value();
  ASSERT_EQ(value, "carrot");
}

} // namespace Utility
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
