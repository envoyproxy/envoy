#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class Argument {
public:
  int value() {
    return 123;
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
