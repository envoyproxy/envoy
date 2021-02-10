#pragma once

#include <memory>

namespace Envoy {
namespace Event {

class DispatcherThreadDeletable {
public:
  virtual ~DispatcherThreadDeletable() = default;
};

using DispatcherThreadDeletablePtr = std::unique_ptr<DispatcherThreadDeletable>;

} // namespace Event
} // namespace Envoy
