#include "test/common/upstream/load_balancer_fuzz_test_utils.h"

namespace Envoy {
namespace Random {
uint64_t FakeRandomGenerator::random() {
  uint8_t index_of_data = counter % bytestring_.size();
  ++counter;
  ENVOY_LOG_MISC(trace, "random() returned: {}", bytestring_.at(index_of_data));
  return bytestring_.at(index_of_data);
}
} // namespace Random
} // namespace Envoy
