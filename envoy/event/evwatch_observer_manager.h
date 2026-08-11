#pragma once

#include <memory>

#include "envoy/event/evwatch.h"

namespace Envoy {
namespace Event {

/**
 * Interface for registering and unregistering Evwatch observers for a single libevent scheduler.
 */
class EvwatchObserverManager {
public:
  virtual ~EvwatchObserverManager() = default;

  /**
   * Registers a non-owning Evwatch observer.
   * @param observer the observer to register.
   */
  virtual void registerObserver(Evwatch::Observer& observer) = 0;

  /**
   * Unregisters a non-owning Evwatch observer and invokes observer.onClose().
   * @param observer the observer to unregister.
   */
  virtual void unregisterObserver(Evwatch::Observer& observer) = 0;
};

using EvwatchObserverManagerPtr = std::unique_ptr<EvwatchObserverManager>;

} // namespace Event
} // namespace Envoy
