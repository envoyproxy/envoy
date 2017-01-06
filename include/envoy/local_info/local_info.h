#pragma once

#include "envoy/common/pure.h"

namespace LocalInfo {

/**
 *
 */
class LocalInfo {
public:
  virtual ~LocalInfo() {}

  virtual const std::string& address() const PURE;
  virtual const std::string& zoneName() const PURE;
  virtual const std::string& clusterName() const PURE;
  virtual const std::string& nodeName() const PURE;
};

} // LocalInfo
