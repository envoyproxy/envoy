#pragma once

#include "envoy/common/pure.h"

namespace LocalInfo {

/**
 * Information about the local environment.
 */
class LocalInfo {
public:
  virtual ~LocalInfo() {}

  /**
   * Human readable network address.
   */
  virtual const std::string& address() const PURE;

  /**
   * Human readable zone name.
   */
  virtual const std::string& zoneName() const PURE;

  /**
   * Human readable cluster name.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Human readable individual node name.
   */
  virtual const std::string& nodeName() const PURE;
};

} // LocalInfo
