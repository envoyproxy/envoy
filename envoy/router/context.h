#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Router {

struct StatNames;
struct VirtualClusterStatNames;

class Context {
public:
  virtual ~Context() = default;

  /**
   * @return a struct containing StatNames for router stats.
   */
  virtual const StatNames& statNames() const PURE;

  /**
   * @return a struct containing StatNames for virtual cluster stats.
   */
  virtual const VirtualClusterStatNames& virtualClusterStatNames() const PURE;
};

} // namespace Router
} // namespace Envoy
