#include "common/http/header_map_impl.h"

namespace Http {

TEST(HeaderMapImplTest, Remove) {
  HeaderMapImpl headers{{"Expect", "100-continue"}};
  EXPECT_EQ("100-continue", headers.get("expect"));
  headers.remove("expect");
  EXPECT_EQ("", headers.get("expect"));
}

} // Http
