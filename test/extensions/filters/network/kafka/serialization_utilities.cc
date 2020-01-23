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
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  absl::FixedArray<Buffer::RawSlice> slices(num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
