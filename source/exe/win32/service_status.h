#pragma once

#include "envoy/common/platform.h"

namespace Envoy {
class ServiceStatus : public SERVICE_STATUS {
public:
  ServiceStatus() {
    static_assert(sizeof(*this) == sizeof(SERVICE_STATUS));
    ::ZeroMemory(this, sizeof(SERVICE_STATUS));
  }
};
} // namespace Envoy
