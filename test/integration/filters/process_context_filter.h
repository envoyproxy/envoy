#pragma once

#include "envoy/server/process_context.h"

namespace Envoy {

class ProcessObjectForFilter : public ProcessObject {
public:
  explicit ProcessObjectForFilter(bool is_healthy) : is_healthy_(is_healthy) {}
  ~ProcessObjectForFilter() override = default;

  bool isHealthy() { return is_healthy_; }

private:
  bool is_healthy_;
};
} // namespace Envoy
