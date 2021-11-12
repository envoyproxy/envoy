#pragma once

#include "source/common/common/c_smart_ptr.h"

struct event_base;
extern "C" {
void event_base_free(event_base*);
}

struct evconnlistener;
extern "C" {
void evconnlistener_free(evconnlistener*);
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

using BasePtr = CSmartPtr<event_base, event_base_free>;

} // namespace Libevent
} // namespace Event
} // namespace Envoy
