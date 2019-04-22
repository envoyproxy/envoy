#include "extensions/filters/network/kafka/header.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

TEST(Something, test) {
  // given
  Something testee;

  // when
  int result = testee.doSomething();

  // then
  ASSERT_EQ(result, 13);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
