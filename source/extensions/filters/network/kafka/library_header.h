#pragma once

#include <memory>

#include "extensions/filters/network/kafka/generated.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class Runner {
public:
  int doSomething() {
    Argument arg;
    const std::unique_ptr<Generated> ptr = std::make_unique<Generated>();
    return ptr->someMethod(arg);
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
