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

TEST_F(UtilityTests, AppendListTest) {
  Protobuf::Arena arena;
  ContainerBackedListImpl list1{{
      CelValue::CreateStringView("foo"),
  }};
  ContainerBackedListImpl list2{{
      CelValue::CreateStringView("bar"),
  }};

  CelList* list3 = appendList(arena, &list1, &list2);
  EXPECT_EQ(list1.size() + list2.size(), list3->size());

  for (int i = 0; i < list1.size(); i++) {
    auto list1_val = list1[i].StringOrDie().value();
    auto list3_val = (*list3)[i].StringOrDie().value();
    EXPECT_EQ(list1_val, list3_val);
  }
  for (int j = 0; j < list2.size(); j++) {
    auto list2_val = list2[j].StringOrDie().value();
    auto list3_val = (*list3)[list1.size() + j].StringOrDie().value();
    EXPECT_EQ(list2_val, list3_val);
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
