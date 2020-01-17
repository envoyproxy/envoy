#include "test/extensions/filters/network/kafka/serialization_utilities.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void assertStringViewIncrement(const absl::string_view incremented,
                               const absl::string_view original, const size_t difference) {

  ASSERT_EQ(incremented.data(), original.data() + difference);
  ASSERT_EQ(incremented.size(), original.size() - difference);
}

const char* getRawData(const Buffer::OwnedImpl& buffer) {
  absl::FixedArray<Buffer::RawSlice> slices(1);
  uint64_t num_slices = buffer.getAtMostNRawSlices(slices.begin(), 1);
  ASSERT(num_slices == 1);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
