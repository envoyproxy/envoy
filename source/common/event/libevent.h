#pragma once

#include "source/common/common/c_smart_ptr.h"

struct event_base;
extern "C" {
void eventBaseFree(event_base*);
}

struct evconnlistener;
extern "C" {
void evconnlistenerFree(evconnlistener*);
}

namespace Envoy {
namespace Event {
namespace Libevent {

/**
 * Global functionality specific to libevent.
 */
class Global {
public:
  static bool initialized() { return initialized_; }

  /**
   * Initialize the library globally.
   */
  static void initialize();

private:
  // True if initialized() has been called.
  static bool initialized_;
};

using BasePtr = CSmartPtr<event_base, eventBaseFree>;

} // namespace Libevent
} // namespace Event
} // namespace Envoy
