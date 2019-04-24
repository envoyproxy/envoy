#include "extensions/filters/network/kafka/library_header.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

TEST(Runner, test) {
  // given
  Runner testee;

  // when
  const int result = testee.doSomething();

  // then
  ASSERT_EQ(result, 125);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
