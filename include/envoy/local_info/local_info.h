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
   * Human readable network address. E.g., "127.0.0.1".
   */
  virtual const std::string& address() const PURE;

  /**
   * Human readable zone name. E.g., "us-east-1a".
   */
  virtual const std::string& zoneName() const PURE;

  /**
   * Human readable cluster name. E.g., "eta".
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Human readable individual node name. E.g., "i-123456".
   */
  virtual const std::string& nodeName() const PURE;
};

} // LocalInfo
