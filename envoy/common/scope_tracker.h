#pragma once

#include <ostream>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {

/*
 * An interface for tracking the scope of work. Implementors of this interface
 * can be registered to the dispatcher when they're active on the stack. If a
 * fatal error occurs while they were active, the dumpState() method will be
 * called to output the active state.
 *
 * Currently this is only used for the L4 network connection and L7 stream.
 */
class ScopeTrackedObject {
public:
  virtual ~ScopeTrackedObject() = default;

  /**
   * Return the tracked stream info that related to the scope tracked object (L4
   * network connection or L7 stream).
   * @return optional reference to stream info of stream (L4 connection or L7 stream).
   */
  virtual OptRef<const StreamInfo::StreamInfo> trackedStream() const { return {}; }

  /**
   * Dump debug state of the object in question to the provided ostream.
   *
   * This is called on Envoy fatal errors, so should do minimal memory allocation.
   *
   * @param os the ostream to output to.
   * @param indent_level how far to indent, for pretty-printed classes and subclasses.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

} // namespace Envoy
