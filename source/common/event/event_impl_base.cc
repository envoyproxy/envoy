#include "event_impl_base.h"

#include "event2/event.h"

namespace Event {

ImplBase::~ImplBase() {
  // Derived classes are assumed to have already assigned the raw event in the constructor.
  event_del(&raw_event_);
}

} // Event
