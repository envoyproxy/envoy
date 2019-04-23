#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class Runner {
public:
  virtual ~Runner() = default;
  virtual int doSomething();
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
