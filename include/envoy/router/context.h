#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Router {

struct StatNames;

class Context {
public:
  virtual ~Context() = default;

  /**
   * @return a struct containing StatNames for router stats.
   */
  virtual const StatNames& statNames() const PURE;
};

} // namespace Router
} // namespace Envoy
