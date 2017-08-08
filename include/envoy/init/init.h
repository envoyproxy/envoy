#pragma once

#include <functional>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Init {

/**
 * A single initialization target.
 */
class Target {
public:
  virtual ~Target() {}

  /**
   * Called when the target should begin its own initialization.
   * @param callback supplies the callback to invoke when the target has completed its
   *        initialization.
   */
  virtual void initialize(std::function<void()> callback) PURE;
};

/**
 * A manager that initializes multiple targets.
 */
class Manager {
public:
  virtual ~Manager() {}

  /**
   * Register a target to be initialized in the future. The manager will call initialize() on
   * each target at some point in the future.
   */
  virtual void registerTarget(Target& target) PURE;
};

} // namespace Init
} // namespace Envoy
