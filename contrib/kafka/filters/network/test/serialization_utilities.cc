#include "contrib/kafka/filters/network/test/serialization_utilities.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void assertStringViewIncrement(const absl::string_view incremented,
                               const absl::string_view original, const size_t difference) {

  ASSERT_EQ(incremented.data(), original.data() + difference);
  ASSERT_EQ(incremented.size(), original.size() - difference);
}

const char* getRawData(const Buffer::Instance& buffer) {
  Buffer::RawSliceVector slices = buffer.getRawSlices(1);
  ASSERT(slices.size() == 1);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
