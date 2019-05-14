#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace StreamInfo {

/**
 * TODO(mergeconflict): document me
 */
class StreamDebugInfo {
public:
  virtual ~StreamDebugInfo() = default;

  virtual bool getAppendClusterInfo() const PURE;
  virtual void setAppendClusterInfo(bool value) PURE;
  virtual bool getDoNotForward() const PURE;
  virtual void setDoNotForward(bool value) PURE;
};

} // namespace StreamInfo
} // namespace Envoy