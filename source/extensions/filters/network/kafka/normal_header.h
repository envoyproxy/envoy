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

class Something1 : public Something {
public:
  Something1(){};

  int someMethod() { return 1; }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
