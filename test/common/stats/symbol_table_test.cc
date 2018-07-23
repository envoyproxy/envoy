#include <string>

#include "common/stats/symbol_table_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class SymbolTableTest : public testing::Test {
public:
  SymbolTableTest() {}

  SymbolTableImpl table_;
};

TEST_F(SymbolTableTest, TestInputExpectations) {
  std::vector<std::string> stat_names = {
      " ",
      "",
      ".",
      "..",
      "f",
      "f.",
      "f.f",
      "fo",
      "foo",
      "some.example.stat_name_1.suffix.extremely_long_name",
      "some.example.stat_name_2.suffix.extremely_long_name",
      "some.example.stat_name_3.suffix.extremely_long_name",
      "some.example.stat_name_4.suffix.extremely_long_name",
      "some.example.stat_name_5.suffix.extremely_long_name",
      "some.example.stat_name_6.suffix.extremely_long_name"
  };

  for (auto stat_name : stat_names) {
    EXPECT_EQ(stat_name, table_.decode(table_.encode(stat_name)));
  }
}

TEST_F(SymbolTableTest, TestShrinkingExpectation) {
  // We expect that as we free stat names, the memory used to store those underlying tokens will be
  // freed.
  size_t table_size_0 = table_.size();

  auto vec_a = table_.encode("a");
  size_t table_size_1 = table_.size();

  auto vec_aa = table_.encode("a.a");
  EXPECT_EQ(table_size_1, table_.size());

  auto vec_ab = table_.encode("a.b");
  size_t table_size_2 = table_.size();

  auto vec_ac = table_.encode("a.c");
  size_t table_size_3 = table_.size();

  auto vec_acd = table_.encode("a.c.d");
  size_t table_size_4 = table_.size();

  auto vec_ace = table_.encode("a.c.e");
  size_t table_size_5 = table_.size();
  EXPECT_GE(table_size_5, table_size_4);

  table_.free(vec_ace);
  EXPECT_EQ(table_size_4, table_.size());

  table_.free(vec_acd);
  EXPECT_EQ(table_size_3, table_.size());

  table_.free(vec_ac);
  EXPECT_EQ(table_size_2, table_.size());

  table_.free(vec_ab);
  EXPECT_EQ(table_size_1, table_.size());

  table_.free(vec_aa);
  EXPECT_EQ(table_size_1, table_.size());

  table_.free(vec_a);
  EXPECT_EQ(table_size_0, table_.size());
}

} // namespace Stats
} // namespace Envoy
