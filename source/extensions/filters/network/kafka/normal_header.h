#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class Something {
public:
  virtual ~Something() = default;
  virtual int someMethod() = 0;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
