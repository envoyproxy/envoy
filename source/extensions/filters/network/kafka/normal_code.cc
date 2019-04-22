#include "extensions/filters/network/kafka/normal_code.h"
#include "extensions/filters/network/kafka/generated_header.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

int NormalCode::doSomething() {
  const GeneratedSomethingPtr ptr = std::make_unique<GeneratedSomething>();
  return 0;
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
