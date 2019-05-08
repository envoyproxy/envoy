#include "test/extensions/filters/network/kafka/serialization_utilities.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void assertStringViewIncrement(absl::string_view incremented, absl::string_view original,
                               size_t difference) {

  ASSERT_EQ(incremented.data(), original.data() + difference);
  ASSERT_EQ(incremented.size(), original.size() - difference);
}

const char* getRawData(const Buffer::OwnedImpl& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

void CapturingRequestCallback::onMessage(AbstractRequestSharedPtr message) {
  captured_.push_back(message);
}

void CapturingRequestCallback::onFailedParse(RequestParseFailureSharedPtr failure_data) {
  parse_failures_.push_back(failure_data);
}

const std::vector<AbstractRequestSharedPtr>& CapturingRequestCallback::getCaptured() const {
  return captured_;
}

const std::vector<RequestParseFailureSharedPtr>&
CapturingRequestCallback::getParseFailures() const {
  return parse_failures_;
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
