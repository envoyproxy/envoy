#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Observer {

class Notifiable {
public:
  virtual ~Notifiable() = default;
  virtual void notify() {}
};

} // namespace Observer
} // namespace Envoy
